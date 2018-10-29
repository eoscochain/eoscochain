#include "icp_relay.hpp"

#include <eosio/producer_plugin/producer_plugin.hpp>
#include <fc/io/json.hpp>

#include "api.hpp"

namespace icp {

listener::listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, relay_ptr relay)
   : acceptor_(ioc), socket_(ioc), relay_(relay) {

   boost::system::error_code ec;

   acceptor_.open(endpoint.protocol(), ec);
   if (ec) {
      log_error(ec, "open");
      return;
   }

   acceptor_.set_option(boost::asio::socket_base::reuse_address(true));

   acceptor_.bind(endpoint, ec);
   if (ec) {
      log_error(ec, "bind");
      return;
   }

   acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
   if (ec) {
      log_error(ec, "listen");
      return;
   }
}

void listener::do_accept() {
   acceptor_.async_accept(socket_, [self=shared_from_this()](auto ec) {
      self->on_accept(ec);
   });
}

void listener::on_accept(boost::system::error_code ec) {
   if (ec) {
      if (ec == boost::system::errc::too_many_files_open) {
         do_accept();
      } else {
         log_error(ec, "accept");
      }
      return;
   }

   try {
      auto s = std::make_shared<session>(move(socket_), relay_);
      relay_->async_add_session(s);
      s->do_accept();
   } catch (std::exception& e) {
      socket_.close();
   }

   do_accept();
}

void relay::start() {
   on_applied_transaction_handle_ = app().get_channel<channels::applied_transaction>().subscribe([this](transaction_trace_ptr t) {
      on_applied_transaction(t);
   });

   on_accepted_block_handle_ = app().get_channel<channels::accepted_block_with_action_digests>().subscribe([this](block_state_with_action_digests_ptr s) {
      on_accepted_block(s);
   });

   on_irreversible_block_handle_ = app().get_channel<channels::irreversible_block>().subscribe([this](block_state_ptr s) {
      on_irreversible_block(s);
   });

   on_bad_block_handle_ = app().get_channel<channels::rejected_block>().subscribe([this](signed_block_ptr b) {
      on_bad_block(b);
   });

   ioc_ = std::make_unique<boost::asio::io_context>(num_threads_);

   timer_ = std::make_shared<boost::asio::deadline_timer>(app().get_io_service());
   start_reconnect_timer();

   auto address = boost::asio::ip::make_address(endpoint_address_);
   listener_ = std::make_shared<listener>(*ioc_, tcp::endpoint{address, endpoint_port_}, shared_from_this());
   listener_->do_accept();

   socket_threads_.reserve(num_threads_);
   for (auto i = 0; i < num_threads_; ++i) {
      socket_threads_.emplace_back([this, i] {
         wlog("start thread ${i}", ("i", i));
         ioc_->run();
         wlog("stop thread ${i}", ("i", i));
      });
   }

   for (const auto& peer: connect_to_peers_) {
      auto s = std::make_shared<session>(peer, *ioc_, shared_from_this());
      sessions_[s.get()] = s;
      s->do_connect();
   }
}

void relay::stop() {
   try {
      timer_->cancel();
      timer_.reset();
   } catch (...) {
      elog("exception thrown on timer shutdown");
   }

   for_each_session([](auto session) {
      session->close();
   });

   listener_.reset();

   ioc_->stop();

   wlog("joining icp relay threads");
   for (auto& t: socket_threads_) {
      t.join();
   }
   wlog("joined icp relay threads");

   for_each_session([](auto session) {
      EOS_ASSERT(false, plugin_exception, "session ${s} still active", ("s", session->session_id_));
   });
}

void relay::start_reconnect_timer() {
   // add some random delay so that all my peers don't attempt to reconnect to me
   // at the same time after shutting down..
   timer_->expires_from_now(boost::posix_time::microseconds(1000000 * (10 + rand() % 5))); // 10+-5 seconds
   timer_->async_wait([=](const boost::system::error_code& ec) {
      if(ec) {
         log_error(ec, "timer wait");
         return;
      }

      verify_strand_in_this_thread(app().get_io_service().get_executor(), __func__, __LINE__);

      for (const auto& peer: connect_to_peers_) {
         bool found = false;
         for (const auto& s: sessions_ ) {
            auto ses = s.second.lock();
            if( ses && (ses->peer_ == peer) ) {
               found = true;
               break;
            }
         }

         if (not found) {
            wlog("attempt to connect to ${p}", ("p", peer));
            auto s = std::make_shared<session>(peer, *ioc_, shared_from_this());
            sessions_[s.get()] = s;
            s->do_connect();
         }
      }

      start_reconnect_timer();
   });
}

void relay::async_add_session(std::weak_ptr<session> s) {
   app().get_io_service().post([s, this] {
      if (auto l = s.lock()) {
         sessions_[l.get()] = s;
      }
   });
}

void relay::on_session_close(const session* s) {
   verify_strand_in_this_thread(app().get_io_service().get_executor(), __func__, __LINE__);
   auto itr = sessions_.find(s);
   if (itr != sessions_.end()) {
      sessions_.erase(itr);
   }
}

void relay::for_each_session(std::function<void (session_ptr)> callback) {
   app().get_io_service().post([this, callback = callback] {
      for (const auto& item : sessions_) {
         if (auto ses = item.second.lock()) {
            ses->post([ses, callback = callback]() {
               callback(ses);
            });
         }
      }
   });
}

void relay::send(const icp_message& msg) {
   for_each_session([m=msg](session_ptr s) mutable {
      s->buffer_send(move(m));
   });
}

void relay::open_channel(const vector<char>& seed) {
   auto& chain = app().get_plugin<chain_plugin>();
   auto rw_api = chain.get_read_write_api();
}

void relay::on_applied_transaction(const transaction_trace_ptr& t) {
   vector<action_name> peer_actions;
   vector<action> actions;
   vector<action_receipt> action_receipts;
   for (auto& action: t->action_traces) {
      if (not (action.act.account == local_contract_ and action.receipt.receiver == action.act.account)) continue;

      if (action.act.name == ACTION_ADDBLOCKS) {
         app().get_io_service().post([this] {
            // update local head
            auto head = get_read_only_api().get_head();
            if (head) {
               local_head_ = *head;
               for_each_session([h=*head](session_ptr s) {
                  s->local_head_ = h;
               });
            }
         });
      }

      if (action.act.name == ACTION_SENDACTION) {
         for (auto& in: action.inline_traces) {
            if (in.act.name == ACTION_ISPACKET) {
               peer_actions.push_back(ACTION_ONPACKET);
               actions.push_back(in.act);
               action_receipts.push_back(in.receipt);
            }
         }
      } else if (action.act.name == ACTION_ONPACKET) {
         for (auto& in: action.inline_traces) {
            if (in.act.name == ACTION_ISRECEIPT) {
               peer_actions.push_back(ACTION_ONRECEIPT);
               actions.push_back(in.act);
               action_receipts.push_back(in.receipt);
            }
         }
      } else if (action.act.name == ACTION_GENPROOF) {
         for (auto& in: action.inline_traces) {
            if (in.act.name == ACTION_ISPACKET || in.act.name == ACTION_ISRECEIPT) {
               peer_actions.push_back(in.act.name == ACTION_ISPACKET ? ACTION_ONPACKET : ACTION_ONRECEIPT);
               actions.push_back(in.act);
               action_receipts.push_back(in.receipt);
            }
         }
      } else if (action.act.name == ACTION_ONCLEANUP) {
         for (auto& in: action.inline_traces) {
            if (in.act.name == ACTION_ISCLEANUP) {
               peer_actions.push_back(ACTION_ONCLEANUP);
               actions.push_back(in.act);
               action_receipts.push_back(in.receipt);
            }
         }
      }
   }

   if (peer_actions.empty()) return;

   auto it = send_transactions_.find(t->id);
   if (it == send_transactions_.end()) return; // TODO
   send_transactions_.insert(send_transaction{t->id, t->block_num, peer_actions, actions, action_receipts});
}

constexpr uint32_t MAX_CACHED_BLOCKS = 1000;
constexpr uint32_t MIN_CACHED_BLOCKS = 100;

void relay::on_accepted_block(const block_state_with_action_digests_ptr& b) {
   bool must_send = false;
   bool may_send = false;

   auto& s = b->block_state;

   // new pending schedule
   if (s->header.new_producers.valid()) {
      must_send = true;
      pending_schedule_version_ = s->pending_schedule.version;
   }

   // new active schedule
   if (s->active_schedule.version == pending_schedule_version_) {
      must_send = true;
      pending_schedule_version_ = 0; // reset
   }

   for (auto& t: s->trxs) {
      auto it = send_transactions_.find(t->id);
      if (it != send_transactions_.end()) {
         may_send = true;

         if (block_with_action_digests_.find(s->id) == block_with_action_digests_.end()) {
            block_with_action_digests_.insert(block_with_action_digests{s->id, b->action_digests});
         }

         break;
      }
   }

   if (not must_send and s->block_num >= peer_head_.head_block_num) {
      auto lag = s->block_num - peer_head_.head_block_num;
      if ((may_send and lag >= MIN_CACHED_BLOCKS) or lag >= MAX_CACHED_BLOCKS) {
         must_send = true;
      }
   }

   if (must_send) {
      auto& chain = app().get_plugin<chain_plugin>();
      vector<block_id_type> merkle_path;
      for (uint32_t i = peer_head_.head_block_num + 1; i < s->block_num; ++i) {
         merkle_path.push_back(chain.chain().get_block_id_for_num(i));
      }

      send(block_header_with_merkle_path{*s, merkle_path});
   }
}

void relay::on_irreversible_block(const block_state_ptr& s) {
   vector<send_transaction> txs;
   for (auto& t: s->trxs) {
      auto it = send_transactions_.find(t->id);
      if (it != send_transactions_.end()) txs.push_back(*it);
   }

   if (txs.empty()) return;
   
   auto bit = block_with_action_digests_.find(s->id);
   if (bit == block_with_action_digests_.end()) {
      elog("cannot find block action digests: block id ${id}", ("id", s->id));
      return;
   }

   icp_actions ia;
   ia.block_header = static_cast<block_header>(s->header);
   ia.action_digests = bit->action_digests;
   for (auto& t: txs) {
      ia.peer_actions.insert(ia.peer_actions.end(), t.peer_actions.cbegin(), t.peer_actions.cend());
      ia.actions.insert(ia.actions.end(), t.actions.cbegin(), t.actions.cend());
      ia.action_receipts.insert(ia.action_receipts.end(), t.action_receipts.cbegin(), t.action_receipts.cend());
   }
   send(ia);
}

void relay::on_bad_block(const signed_block_ptr& b) {
}

void relay::push_transaction(vector<action> actions, packed_transaction::compression_type compression) {
   auto& chain = app().get_plugin<chain_plugin>();

   signed_transaction trx;
   trx.actions = std::forward<decltype(actions)>(actions);
   for (auto& a: trx.actions) {
      a.account = signer_.front().actor;
      a.authorization = signer_;
   }

   trx.expiration = chain.chain().head_block_time() + tx_expiration_;
   trx.set_reference_block(chain.chain().last_irreversible_block_id());
   trx.max_cpu_usage_ms = tx_max_cpu_usage_;
   trx.max_net_usage_words = (tx_max_net_usage_ + 7)/8;
   trx.delay_sec = delaysec_;

   auto pp = app().find_plugin<producer_plugin>();
   FC_ASSERT(pp and pp->get_state() == abstract_plugin::started, "producer_plugin not found");

   if (signer_required_keys_.empty()) {
      auto available_keys = pp->get_producer_keys();
      auto ro_api = chain.get_read_only_api();
      fc::variant v{static_cast<const transaction&>(trx)};
      signer_required_keys_ = ro_api.get_required_keys(chain_apis::read_only::get_required_keys_params{v, available_keys}).required_keys;
   }

   auto digest = trx.sig_digest(chain.get_chain_id(), trx.context_free_data);
   for (auto& k: signer_required_keys_) {
      trx.signatures.push_back(pp->sign_compact(k, digest));
   }

   auto packet_tx = fc::mutable_variant_object(packed_transaction(trx, compression));
   auto rw_api = chain.get_read_write_api();
   rw_api.push_transaction(fc::variant_object(packet_tx), [](const fc::static_variant<fc::exception_ptr, chain_apis::read_write::push_transaction_results>& result) {
      if (result.contains<fc::exception_ptr>()) {
         result.get<fc::exception_ptr>()->dynamic_rethrow_exception();
      } else {
         auto& r = result.get<chain_apis::read_write::push_transaction_results>();
         wlog("transaction ${id}: ${processed}", ("id", r.transaction_id)("processed", fc::json::to_string(r.processed)));
         // TODO
      }
   });
}

}

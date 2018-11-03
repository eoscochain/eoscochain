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
      s->maybe_send_next_message();
   });
}

void relay::open_channel(const block_header_state& seed) {
   send(channel_seed{seed});
}

void relay::update_local_head() {
   // update local head
   head_ptr h;
   try_catch([&h, this]() mutable {
      h = get_read_only_api().get_head();
   });
   if (h and (h->head_block_id != local_head_.head_block_id
              or h->last_irreversible_block_id != local_head_.last_irreversible_block_id)) {

      local_head_ = *h;

      for (auto it = recv_transactions_.begin(); it != recv_transactions_.end();) {
         if (it->block_num > local_head_.last_irreversible_block_num) break;

         auto rt = *it;
         push_icp_actions(move(rt));
         it = recv_transactions_.erase(it);
      }

      for_each_session([h=*h](session_ptr s) {
         s->update_local_head(h);
         s->maybe_send_next_message();
      });
   }
}

void relay::on_applied_transaction(const transaction_trace_ptr& t) {
   if (send_transactions_.find(t->id) != send_transactions_.end()) return; // has been handled

   vector<action_name> peer_actions;
   vector<action> actions;
   vector<action_receipt> action_receipts;
   for (auto& action: t->action_traces) {
      if (action.receipt.receiver != action.act.account) continue;

      if (action.act.account != local_contract_ or action.act.name == ACTION_DUMMY) { // thirdparty contract call or icp contract dummy call
         for (auto& in: action.inline_traces) {
            if (in.receipt.receiver != in.act.account) continue;
            if (in.act.account == local_contract_ and in.act.name == ACTION_SENDACTION) {
               for (auto &inin: in.inline_traces) {
                  if (inin.receipt.receiver != inin.act.account) continue;
                  if (inin.act.name == ACTION_ISPACKET) {
                     // wlog("ispacket: ${a}, ${n}", ("a", inin.act.account)("n", inin.act.name));
                     peer_actions.push_back(ACTION_ONPACKET);
                     actions.push_back(inin.act);
                     action_receipts.push_back(inin.receipt);
                  }
               }
            }
         }
      } else if (action.act.account == local_contract_) {
         if (action.act.name == ACTION_ADDBLOCKS or action.act.name == ACTION_OPENCHANNEL) {
            app().get_io_service().post([this] {
               update_local_head();
            });
         } else if (action.act.name == ACTION_ONPACKET) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISRECEIPT) {
                  peer_actions.push_back(ACTION_ONRECEIPT);
                  actions.push_back(in.act);
                  action_receipts.push_back(in.receipt);
               }
            }
         } else if (action.act.name == ACTION_GENPROOF) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISPACKET || in.act.name == ACTION_ISRECEIPT) {
                  peer_actions.push_back(in.act.name == ACTION_ISPACKET ? ACTION_ONPACKET : ACTION_ONRECEIPT);
                  actions.push_back(in.act);
                  action_receipts.push_back(in.receipt);
               }
            }
         } else if (action.act.name == ACTION_ONCLEANUP) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISCLEANUP) {
                  peer_actions.push_back(ACTION_ONCLEANUP);
                  actions.push_back(in.act);
                  action_receipts.push_back(in.receipt);
               }
            }
         }
      }
   }

   if (peer_actions.empty()) return;

   send_transactions_.insert(send_transaction{t->id, t->block_num, peer_actions, actions, action_receipts});
}

void relay::clear_cache_block_state() {
   block_states_.clear();
   wlog("clear_cache_block_state");
}

void relay::cache_block_state(block_state_ptr b) {
   auto& idx = block_states_.get<by_num>();
   for (auto it = idx.begin(); it != idx.end();) {
      if (it->block_num + 500 < b->block_num) {
         it = idx.erase(it);
      } else {
         break;
      }
   }
   auto it = idx.find(b->block_num);
   if (it != idx.end()) {
      idx.erase(it);
   }

   block_states_.insert(static_cast<const block_header_state&>(*b));
   // wlog("cache_block_state");
}

void relay::on_accepted_block(const block_state_with_action_digests_ptr& b) {
   bool must_send = false;
   bool may_send = false;

   auto& s = b->block_state;

   if (not peer_head_.valid()) {
      cache_block_state(s);
   }

   // new pending schedule
   if (s->header.new_producers.valid()) {
      must_send = true;
      pending_schedule_version_ = s->pending_schedule.version;
   }

   // new active schedule
   if (s->active_schedule.version > 0 and s->active_schedule.version == pending_schedule_version_) {
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

   if (not must_send and peer_head_.valid() and s->block_num >= peer_head_.head_block_num) {
      auto lag = s->block_num - peer_head_.head_block_num;
      if ((may_send and lag >= MIN_CACHED_BLOCKS) or lag >= MAX_CACHED_BLOCKS) {
         must_send = true;
      }
   }

   if (must_send) {
      auto& chain = app().get_plugin<chain_plugin>();
      vector<block_id_type> merkle_path;
      for (uint32_t i = peer_head_.head_block_num; i < s->block_num; ++i) {
         merkle_path.push_back(chain.chain().get_block_id_for_num(i));
      }

      send(block_header_with_merkle_path{*s, merkle_path});
   } else {
      update_local_head();
   }
}

void relay::on_irreversible_block(const block_state_ptr& s) {
   vector<send_transaction> txs;
   for (auto& t: s->trxs) {
      auto it = send_transactions_.find(t->id);
      if (it != send_transactions_.end()) txs.push_back(*it);
   }

   if (txs.empty()) {
      if (fc::time_point::now() - last_transaction_time_ >= fc::seconds(DUMMY_ICP_SECONDS) and peer_head_.valid()) {
         app().get_io_service().post([=] {
            action a;
            a.name = ACTION_DUMMY;
            a.data = fc::raw::pack(dummy{signer_[0].actor});
            push_transaction(vector<action>{a});
         });
      }
      return;
   }

   last_transaction_time_ = fc::time_point::now();

   auto bit = block_with_action_digests_.find(s->id);
   if (bit == block_with_action_digests_.end()) {
      elog("cannot find block action digests: block id ${id}", ("id", s->id));
      return;
   }

   if (s->block_num > peer_head_.head_block_num) {
      auto& chain = app().get_plugin<chain_plugin>();
      vector<block_id_type> merkle_path;
      for (uint32_t i = peer_head_.head_block_num; i < s->block_num; ++i) {
         merkle_path.push_back(chain.chain().get_block_id_for_num(i));
      }

      send(block_header_with_merkle_path{*s, merkle_path});
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

void relay::handle_icp_actions(recv_transaction&& rt) {
   if (local_head_.last_irreversible_block_num < rt.block_num) {
      recv_transactions_.insert(move(rt)); // cache it, push later
      return;
   }

   push_icp_actions(move(rt));
}

void relay::push_icp_actions(recv_transaction&& rt) {
   app().get_io_service().post([=] {
      if (not rt.action_add_block.account.empty()) {
         push_transaction(vector<action>{rt.action_add_block});
      }
      // TODO: rate limiting, cache, and retry
      for (auto& a: rt.action_icp) {
         wlog("action_icp: ${a}, ${n}", ("a", a.name)("n", a.data.size()));
         push_transaction(vector<action>{a});
      }
   });
}

void print_action( const fc::variant& at ) {
   const auto& receipt = at["receipt"];
   auto receiver = receipt["receiver"].as_string();
   const auto& act = at["act"].get_object();
   auto code = act["account"].as_string();
   auto func = act["name"].as_string();
   auto args = fc::json::to_string( act["data"] );
   auto console = at["console"].as_string();

   if( args.size() > 100 ) args = args.substr(0,100) + "...";
   cout << "#" << std::setw(14) << right << receiver << " <= " << std::setw(28) << std::left << (code +"::" + func) << " " << args << "\n";
   if( console.size() ) {
      std::stringstream ss(console);
      string line;
      std::getline( ss, line );
      cout << ">> " << line << "\n";
   }
}

void print_action_tree( const fc::variant& action ) {
   print_action( action );
   const auto& inline_traces = action["inline_traces"].get_array();
   for( const auto& t : inline_traces ) {
      print_action_tree( t );
   }
}

void print_result(const fc::variant& processed) { try {
   const auto& transaction_id = processed["id"].as_string();
   string status = processed["receipt"].is_object() ? processed["receipt"]["status"].as_string() : "failed";
   int64_t net = -1;
   int64_t cpu = -1;
   if( processed.get_object().contains( "receipt" )) {
      const auto& receipt = processed["receipt"];
      if( receipt.is_object()) {
         net = receipt["net_usage_words"].as_int64() * 8;
         cpu = receipt["cpu_usage_us"].as_int64();
      }
   }

   cerr << status << " transaction: " << transaction_id << "  ";
   if( net < 0 ) {
      cerr << "<unknown>";
   } else {
      cerr << net;
   }
   cerr << " bytes  ";
   if( cpu < 0 ) {
      cerr << "<unknown>";
   } else {
      cerr << cpu;
   }

   cerr << " us\n";

   if( status == "failed" ) {
      auto soft_except = processed["except"].as<optional<fc::exception>>();
      if( soft_except ) {
         edump((soft_except->to_detail_string()));
      }
   } else {
      const auto& actions = processed["action_traces"].get_array();
      for( const auto& a : actions ) {
         print_action_tree( a );
      }
      wlog( "\rwarning: transaction executed locally, but may not be confirmed by the network yet" );
   }
} FC_CAPTURE_AND_RETHROW( (processed) ) }

void relay::push_transaction(vector<action> actions, function<void(bool)> callback, packed_transaction::compression_type compression) {
   auto& chain = app().get_plugin<chain_plugin>();

   signed_transaction trx;
   trx.actions = std::forward<decltype(actions)>(actions);
   vector<action_name> action_names;
   for (auto& a: trx.actions) {
      a.account = local_contract_;
      if (a.authorization.empty()) a.authorization = signer_;
      action_names.push_back(a.name);
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
   rw_api.push_transaction(fc::variant_object(packet_tx), [action_names, callback](const fc::static_variant<fc::exception_ptr, chain_apis::read_write::push_transaction_results>& result) {
      if (result.contains<fc::exception_ptr>()) {
         wlog("actions: ${a}", ("a", action_names));
         elog("${e}", ("e", result.get<fc::exception_ptr>()->to_detail_string()));
         if (callback) callback(false);
      } else {
         auto& r = result.get<chain_apis::read_write::push_transaction_results>();
         // wlog("transaction ${id}: ${processed}", ("id", r.transaction_id)("processed", fc::json::to_string(p)));
         print_result(r.processed);
         if (callback) callback(true);
         // TODO
      }
   });
}

}

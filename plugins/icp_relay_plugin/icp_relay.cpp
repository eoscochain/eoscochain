#include "icp_relay.hpp"

#include "api.hpp"
#include "message.hpp"

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
               // wlog("use count: ${s}, ${ss}", ("s", s.second.use_count())("ss", ses.use_count()));
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

void relay::update_local_head(bool force) {
   // update local head
   head_ptr h;
   sequence_ptr s;
   try_catch([&h, &s, this]() mutable {
      h = get_read_only_api().get_head();
      s = get_read_only_api().get_sequence();
   });
   // wlog("head: ${h}, ${hh}", ("h", bool(h))("hh", h ? *h : head{}));
   if (h and s and (force or h->head_block_id != local_head_.head_block_id
                     or h->last_irreversible_block_id != local_head_.last_irreversible_block_id)) { // local head changed

      local_head_ = *h;
      // wlog("head: ${h}", ("h", local_head_));

      std::unordered_set<packet_receipt_request> req_set; // deduplicate

      for (auto it = recv_transactions_.begin(); it != recv_transactions_.end();) {
         if (it->block_num > local_head_.last_irreversible_block_num) break;

         // wlog("last_incoming_packet_seq: ${lp}, last_incoming_receipt_seq: ${lr}, start_packet_seq: ${sp}, start_receipt_seq: ${sr}", ("lp", s->last_incoming_packet_seq)("lr", s->last_incoming_receipt_seq)("sp", it->start_packet_seq)("sr", it->start_receipt_seq));
         auto req = s->make_genproof_request(it->start_packet_seq, it->start_receipt_seq);
         if (not req.empty()) {
            if (not req_set.count(req)) {
               send(req);
               req_set.insert(req);
            }
            ++it;
            continue;
         }

         auto rt = *it;
         push_icp_actions(s, move(rt));
         it = recv_transactions_.erase(it);
      }

      for_each_session([h=*h](session_ptr s) {
         // wlog("has session");
         s->update_local_head(h); // TODO: ?
      });
      send(head_notice{local_head_});
   }
}

void relay::on_applied_transaction(const transaction_trace_ptr& t) {
   if (send_transactions_.find(t->id) != send_transactions_.end()) return; // has been handled

   send_transaction st{t->id, t->block_num};

   for (auto& action: t->action_traces) {
      if (action.receipt.receiver != action.act.account) continue;

      if (action.act.account != local_contract_ or action.act.name == ACTION_DUMMY) { // thirdparty contract call or icp contract dummy call
         for (auto& in: action.inline_traces) {
            if (in.receipt.receiver != in.act.account) continue;
            if (in.act.account == local_contract_ and in.act.name == ACTION_SENDACTION) {
               for (auto &inin: in.inline_traces) {
                  if (inin.receipt.receiver != inin.act.account) continue;
                  if (inin.act.name == ACTION_ISPACKET) {
                     auto seq = icp_packet::get_seq(inin.act.data, st.start_packet_seq);
                     st.packet_actions[seq] = send_transaction_internal{ACTION_ONPACKET, inin.act, inin.receipt};
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
                  auto seq = icp_receipt::get_seq(in.act.data, st.start_receipt_seq);
                  st.receipt_actions[seq] = send_transaction_internal{ACTION_ONRECEIPT, in.act, in.receipt};
               }
            }
         } else if (action.act.name == ACTION_GENPROOF) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISPACKET) {
                  auto seq = icp_packet::get_seq(in.act.data, st.start_packet_seq);
                  st.packet_actions[seq] = send_transaction_internal{ACTION_ONPACKET, in.act, in.receipt};
               } else if (in.act.name == ACTION_ISRECEIPT) {
                  auto seq = icp_receipt::get_seq(in.act.data, st.start_receipt_seq);
                  st.receipt_actions[seq] = send_transaction_internal{ACTION_ONRECEIPT, in.act, in.receipt};
               } else if (in.act.name == ACTION_ISRECEIPTEND) {
                  st.receiptend_actions.push_back(send_transaction_internal{ACTION_ONRECEIPTEND, in.act, in.receipt});
               }
            }
         /*} else if (action.act.name == ACTION_CLEANUP) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISCLEANUP) {
                  st.cleanup_actions.push_back(send_transaction_internal{ACTION_ONCLEANUP, in.act, in.receipt});
               }
            }*/
         } else if (action.act.name == ACTION_ONRECEIPT) {
            for (auto &in: action.inline_traces) {
               if (in.receipt.receiver != in.act.account) continue;
               if (in.act.name == ACTION_ISRECEIPTEND) {
                  st.receiptend_actions.push_back(send_transaction_internal{ACTION_ONRECEIPTEND, in.act, in.receipt});
               }
            }
            // cleanup_sequences();
         }
      }
   }

   if (st.empty()) return;

   send_transactions_.insert(st);
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
      for_each_session([](session_ptr s) mutable {
         s->maybe_send_next_message();
      });
   }

   try_catch([this]() mutable {
      cleanup();
   });
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
         last_transaction_time_ = fc::time_point::now();
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
   ia.block_header_instance = static_cast<block_header>(s->header);
   ia.action_digests = bit->action_digests;

   std::map<uint64_t, send_transaction_internal> packet_actions; // key is packet seq
   std::map<uint64_t, send_transaction_internal> receipt_actions; // key is receipt seq
   for (auto& t: txs) {
      packet_actions.insert(t.packet_actions.cbegin(), t.packet_actions.cend());
      receipt_actions.insert(t.receipt_actions.cbegin(), t.receipt_actions.cend());
      for (auto& c: t.receiptend_actions) {
         ia.receiptend_actions.push_back(c);
      }
      ia.set_seq(t.start_packet_seq, t.start_receipt_seq);
   }
   for (auto& p: packet_actions) {
      ia.packet_actions.emplace_back(p.first, p.second);
   }
   for (auto& r: receipt_actions) {
      ia.receipt_actions.emplace_back(r.first, r.second);
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

   auto s = get_read_only_api().get_sequence();
   if (s) {
      auto req = s->make_genproof_request(rt.start_packet_seq, rt.start_receipt_seq);
      if (not req.empty()) {
         send(req);
         recv_transactions_.insert(move(rt)); // cache it, push later
         return;
      }
   } else {
      elog("got empty sequence");
   }

   push_icp_actions(s, move(rt));
}

void relay::push_icp_actions(const sequence_ptr& s, recv_transaction&& rt) {
   app().get_io_service().post([=] {
      if (not rt.action_add_block.name.empty()) {
         push_transaction(vector<action>{rt.action_add_block});
      }

      // TODO: rate limiting, cache, and retry
      auto packet_seq = s->last_incoming_packet_seq + 1; // strictly increasing, otherwise fail
      auto receipt_seq = s->last_incoming_receipt_seq + 1; // strictly increasing, otherwise fail
      for (auto& p: rt.packet_actions) {
         if (p.first != packet_seq) continue;
         push_transaction(vector<action>{p.second});
         ++packet_seq;
      }
      for (auto& r: rt.receipt_actions) {
         wlog("last_incoming_receipt_seq: ${lr}, receipt seq: ${rs}", ("lr", receipt_seq)("rs", r.first));
         if (r.first != receipt_seq) continue;
         push_transaction(vector<action>{r.second});
         ++receipt_seq;
      }
      for (auto& a: rt.receiptend_actions) {
         push_transaction(vector<action>{a});
      }
   });
}

void relay::cleanup() {
   if (not local_head_.valid()) return;

   ++cumulative_cleanup_count_;
   if (cumulative_cleanup_count_ < MAX_CLEANUP_NUM) return;

   auto s = get_read_only_api().get_sequence(true);
   if (not s) {
      elog("got empty sequence");
      return;
   }

   // wlog("sequence: ${s}", ("s", *s));

   auto cleanup_packet = (s->min_packet_seq > 0 and s->last_incoming_receipt_seq > s->min_packet_seq) ? (s->last_incoming_receipt_seq - s->min_packet_seq) : 0; // TODO: consistent receipt and packet sequence?
   auto cleanup_receipt = (s->min_receipt_seq > 0 and s->last_finalised_outgoing_receipt_seq > s->min_receipt_seq) ? (s->last_finalised_outgoing_receipt_seq - s->min_receipt_seq) : 0;
   auto cleanup_block = (s->min_block_num > 0 and s->max_finished_block_num() > s->min_block_num) ? (s->max_finished_block_num() - s->min_block_num) : 0;
   if (cleanup_packet >= MAX_CLEANUP_NUM or cleanup_receipt >= MAX_CLEANUP_NUM or cleanup_block >= MAX_CLEANUP_NUM) {
      app().get_io_service().post([=] {
         action a;
         a.name = ACTION_CLEANUP;
         a.data = fc::raw::pack(icp::cleanup{MAX_CLEANUP_NUM});
         wlog("cleanup *************************************");
         push_transaction(vector<action>{a});
      });
   }

   if (cleanup_packet + cleanup_receipt + cleanup_block < 2*MAX_CLEANUP_NUM) {
      cumulative_cleanup_count_ = 0;
   }
}

/*void relay::cleanup_sequences() {
   ++cumulative_cleanup_sequences_;
   if (cumulative_cleanup_sequences_ < MAX_CLEANUP_SEQUENCES) return;

   auto s = get_read_only_api().get_sequence(true);
   if (not s) {
      elog("got empty sequence");
      return;
   }

   wlog("min_packet_seq: ${m}, last_incoming_receipt_seq: ${r}", ("m", s->min_packet_seq)("r", s->last_incoming_receipt_seq));

   if (s->min_packet_seq > 0 and s->last_incoming_receipt_seq - s->min_packet_seq >= MAX_CLEANUP_SEQUENCES) { // TODO: consistent receipt and packet sequence?
      app().get_io_service().post([=] {
         action a;
         a.name = ACTION_CLEANUP;
         a.data = fc::raw::pack(cleanup{s->min_packet_seq, s->last_incoming_receipt_seq});
         wlog("cleanup: ${s} -> ${e}", ("s", s->min_packet_seq)("e", s->last_incoming_receipt_seq));
         push_transaction(vector<action>{a});
      });
   }
   cumulative_cleanup_sequences_ = 0;
}*/

}

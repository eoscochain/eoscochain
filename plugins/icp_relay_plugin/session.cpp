#include "session.hpp"

#include <appbase/application.hpp>

#include "icp_relay.hpp"

namespace icp {

// Creating session from server socket acceptance
session::session(tcp::socket socket, relay_ptr relay)
   : ios_(socket.get_io_service()),
     resolver_(socket.get_io_service()),
     ws_(std::make_unique<ws::stream<tcp::socket>>(std::move(socket))),
     strand_(ws_->get_executor()),
     relay_(relay) {

   session_id_ = next_session_id();
   set_socket_options();
   ws_->binary(true);
   wlog("open session ${id}", ("id", session_id_));
}

void session::do_accept() {
   ws_->async_accept(boost::asio::bind_executor(strand_, [this, self=shared_from_this()](boost::system::error_code ec) {
      if (ec) {
         return on_error(ec, "accept");
      }

      do_hello();
      do_read();
   }));
}

// Creating outgoing session
session::session(const string& peer, boost::asio::io_context& ioc, relay_ptr relay)
   : ios_(ioc),
     resolver_(ioc),
     ws_(std::make_unique<ws::stream<tcp::socket>>(ioc)),
     strand_(ws_->get_executor()),
     relay_(relay) {

   session_id_ = next_session_id();
   ws_->binary(true);
   wlog("open session ${id}", ("id", session_id_));

   peer_ = peer;
   auto c = peer.find(':');
   remote_host_ = peer.substr(0, c);
   remote_port_ = peer.substr(c+1, peer.size());
}

void session::do_connect() {
   resolver_.async_resolve(remote_host_, remote_port_,
      boost::asio::bind_executor(strand_, [this, self=shared_from_this()](boost::system::error_code ec, tcp::resolver::results_type results) {
         if (ec) {
            return on_error(ec, "resolve");
         }

         // connect
         boost::asio::async_connect(ws_->next_layer(),
            results.begin(), results.end(),
            boost::asio::bind_executor(strand_,
               std::bind(&session::on_connect, self, std::placeholders::_1)
            )
         );
      })
   );
}

void session::on_connect(boost::system::error_code ec) {
   if (ec) {
      return on_error(ec, "connect");
   }

   set_socket_options();

   // handshake
   ws_->async_handshake(remote_host_, "/",
      boost::asio::bind_executor(strand_, [this, self=shared_from_this()](boost::system::error_code ec) {
         if (ec) {
            return on_error(ec, "handshake");
         }

         do_hello();
         do_read();
      })
   );
}

session::~session() {
   wlog("close session ${n}", ("n", session_id_));
   std::weak_ptr<relay> r = relay_;
   app().get_io_service().post([r, s=this] {
      if (auto relay = r.lock()) relay->on_session_close(s);
   });
}

int session::next_session_id() {
   static int session_count = 0;
   return ++session_count;
}

void session::set_socket_options() {
   try {
      /** to minimize latency when sending short messages */
      ws_->next_layer().set_option(boost::asio::ip::tcp::no_delay(true));

      /** to minimize latency when sending large 1MB blocks, the send buffer should not have to
       * wait for an "ack", making this larger could result in higher latency for smaller urgent
       * messages.
       */
      ws_->next_layer().set_option(boost::asio::socket_base::send_buffer_size(1024*1024));
      ws_->next_layer().set_option(boost::asio::socket_base::receive_buffer_size(1024*1024));
   } catch (...) {
      elog("uncaught exception on set socket options");
   }
}

void session::on_error(boost::system::error_code ec, const char* what) {
   try {
      verify_strand_in_this_thread(strand_, __func__, __LINE__);
      elog("${w}: ${m}", ("w", what)("m", ec.message())); // TODO
      ws_->next_layer().close();
   } catch (...) {
      elog("uncaught exception on close");
   }
}

void session::close() {
   try {
      ws_->next_layer().close();
   } catch (...) {
      elog("uncaught exception on close");
   }
}

void session::post(std::function<void()> callback) {
   ios_.post(boost::asio::bind_executor(strand_, callback));
}

void session::do_hello() {
   hello hello_msg;
   hello_msg.id = relay_->id_;
   hello_msg.chain_id = app().get_plugin<chain_plugin>().get_chain_id();
   hello_msg.contract = relay_->local_contract_;
   hello_msg.peer_contract = relay_->peer_contract_;
   send(hello_msg);
   sent_remote_hello_ = true;
}

void session::do_read() {
   ws_->async_read(in_buffer_,
       boost::asio::bind_executor(strand_, [this, self=shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred) {
          if (ec == ws::error::closed) return on_error(ec, "close on read");
          else if (ec) return on_error(ec, "read");

          try {
             auto data = boost::asio::buffer_cast<char const*>(boost::beast::buffers_front(in_buffer_.data()));
             auto size = boost::asio::buffer_size(in_buffer_.data());
             fc::datastream<const char*> ds(data, size);

             icp_message msg;
             fc::raw::unpack(ds, msg);
             on_message(msg);
             in_buffer_.consume(ds.tellp());

             wait_on_app();

          } catch (...) {
             wlog("close bad payload");
             try {
                ws_->close(boost::beast::websocket::close_code::bad_payload);
             } catch ( ... ) {
                elog("uncaught exception on close");
             }
          }
       })
   );
}

/** If we just call do_read here then this thread might run ahead of
 * the main thread, instead we post an event to main which will then
 * post a new read event when ready.
 *
 * This also keeps the "shared pointer" alive in the callback preventing
 * the connection from being closed.
 */
void session::wait_on_app() {
   app().get_io_service().post(
      boost::asio::bind_executor(strand_, [self = shared_from_this()] {
         self->do_read();
      })
   );
}

bool session::send_ping() {
   auto delta_t = fc::time_point::now() - last_sent_ping_.sent;
   if (delta_t < fc::seconds(3)) return false;

   if (not local_head_.valid()) {
      // wlog("local head: ${h}", ("h", local_head_));
      app().get_io_service().post(
         boost::asio::bind_executor(strand_, [this, self = shared_from_this()] {
            relay_->update_local_head(true);
            maybe_send_next_message();
         })
      );
      last_sent_ping_.sent = fc::time_point::now();
      return false;
   }

   if (last_sent_ping_.code == fc::sha256()) {
      last_sent_ping_.sent = fc::time_point::now();
      last_sent_ping_.code = fc::sha256::hash(last_sent_ping_.sent); /// TODO: make this more random
      last_sent_ping_.head_instance = local_head_;
      send(last_sent_ping_);
      // wlog("send_ping");
   }
   return true;
}

bool session::send_pong() {
   if (last_recv_ping_.code == fc::sha256()) return false;

   send(pong{fc::time_point::now(), last_recv_ping_.code});
   last_recv_ping_.code = fc::sha256(); // reset
   return true;
}

void session::send() {
   try {
      verify_strand_in_this_thread(strand_, __func__, __LINE__);

      state_ = sending_state;
      ws_->async_write(boost::asio::buffer(out_buffer_),
                       boost::asio::bind_executor(strand_,
                          [this, self=shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred) {
                          verify_strand_in_this_thread(strand_, __func__, __LINE__);
                          if (ec) {
                            ws_->next_layer().close();
                            return on_error(ec, "write");
                          }
                          state_ = idle_state;
                          out_buffer_.resize(0);
                          maybe_send_next_message();
                       })
      );
   } FC_LOG_AND_RETHROW()
}

void session::send(const icp_message& msg) {
   try {
      auto ps = fc::raw::pack_size(msg);
      out_buffer_.resize(ps);
      fc::datastream<char*> ds(out_buffer_.data(), ps);
      fc::raw::pack(ds, msg);
      send();
   } FC_LOG_AND_RETHROW()
}

void session::buffer_send(icp_message&& msg) {
   msg_buffer_.push_back(move(msg));
}

void session::maybe_send_next_message() {
   verify_strand_in_this_thread(strand_, __func__, __LINE__);
   if (state_ == sending_state) return; // in process of sending
   if (out_buffer_.size()) return; // in process of sending
   if (!recv_remote_hello_ || !sent_remote_hello_) return;

   if (send_pong()) return;
   if (send_ping()) return;

   // ilog("msg buffer count: ${n}", ("n", msg_buffer_.size()));

   if (not msg_buffer_.empty()) {
      auto msg = msg_buffer_.front();
      send(msg);
      msg_buffer_.pop_front();
   }
   // TODO
}

void session::update_local_head(const head& h) {
   local_head_ = h;
}

void session::on_message(const icp_message& msg) {
   try {
      switch (msg.which()) {
         case icp_message::tag<hello>::value:
            // wlog("on hello");
            on(msg.get<hello>());
            break;
         case icp_message::tag<ping>::value:
            // wlog("on ping");
            on(msg.get<ping>());
            break;
         case icp_message::tag<pong>::value:
            // wlog("on pong");
            on(msg.get<pong>());
            break;
         case icp_message::tag<channel_seed>::value:
            // wlog("on channel_seed");
            on(msg.get<channel_seed>());
            break;
         case icp_message::tag<head_notice>::value:
            // wlog("on head_notice");
            on(msg.get<head_notice>());
            break;
         case icp_message::tag<block_header_with_merkle_path>::value:
            // wlog("on block_header_with_merkle_path");
            on(msg.get<block_header_with_merkle_path>());
            break;
         case icp_message::tag<icp_actions>::value:
            // wlog("on icp_actions");
            on(msg.get<icp_actions>());
            break;
         case icp_message::tag<packet_receipt_request>::value:
            // wlog("on packet_receipt_request");
            on(msg.get<packet_receipt_request>());
            break;
         default:
            wlog("bad message received");
            ws_->close(boost::beast::websocket::close_code::bad_payload);
            return;
      }

      maybe_send_next_message();

   } catch (const fc::exception& e) {
      elog("${e}", ("e", e.to_detail_string()));
      ws_->close(boost::beast::websocket::close_code::bad_payload);
   }
}

void session::check_for_redundant_connection() {
   app().get_io_service().post([self=shared_from_this()] {
      self->relay_->for_each_session([self](auto s) {
         if (s != self && s->peer_id_ == self->peer_id_) {
            self->close();
         }
      });
   });
}

void session::on(const hello& hi) {
   // ilog("received hello: peer id ${id}, peer chain id ${chain_id}, peer icp contract ${contract}, refer to my contract ${peer_contract}", ("id", hi.id)("chain_id", hi.chain_id)("contract", hi.contract)("peer_contract", hi.peer_contract));

   recv_remote_hello_ = true;

   if (hi.chain_id != relay_->peer_chain_id_) {
      elog("bad peer: wrong chain id");
      return close();
   }

   if (hi.id == relay_->id_) {
      // connect to self
      return close();
   }

   peer_id_ = hi.id;

   check_for_redundant_connection();
}

void session::on(const ping& p) {
   last_recv_ping_ = p;
   last_recv_ping_time_ = fc::time_point::now();

   // wlog("on ping: ${v}", ("v", p.head.valid()));

   if (not p.head_instance.valid()) return;

   app().get_io_service().post([=, self=shared_from_this()] {
      if (not relay_->peer_head_.valid()) {
         relay_->clear_cache_block_state();
      }
      relay_->peer_head_ = p.head_instance; // TODO: check validity
   });
}

void session::on(const pong& p) {
   if (p.code != last_sent_ping_.code) {
      close();
      return;
   }
   last_sent_ping_.code = fc::sha256(); // reset
}

void session::on(const channel_seed& s) {
   auto data = fc::raw::pack(bytes_data{fc::raw::pack(s.seed)});
   app().get_io_service().post([=, self=shared_from_this()] {
      action a;
      a.name = ACTION_OPENCHANNEL;
      a.data = data;
      // ilog("data: ${d}", ("d", a.data));
      a.authorization.push_back(permission_level{relay_->local_contract_, permission_name("active")});
      relay_->push_transaction(vector<action>{a});
   });
}

void session::on(const head_notice& h) {
   // wlog("recv head: ${v}", ("v", h.head.valid()));
   if (not h.head_instance.valid()) return;

   app().get_io_service().post([=, self=shared_from_this()] {
      if (not relay_->peer_head_.valid()) {
         relay_->clear_cache_block_state();
      }
      relay_->peer_head_ = h.head_instance; // TODO: check validity
   });
}

void session::on(const block_header_with_merkle_path& b) {
   auto ro = relay_->get_read_only_api();
   auto head = ro.get_head();

   if (not head) {
      elog("local head not found, maybe icp channel not opened");
      return;
   }

   auto first_num = b.block_header.block_num;
   if (not b.merkle_path.empty()) {
      first_num = block_header::num_from_id(b.merkle_path.front());
   }

   // wlog("block_header_with_merkle_path: ${n1} -> ${n2}, head: ${h}", ("n1", first_num)("n2", b.block_header.block_num)("h", head->head_block_num));

   if (first_num != head->head_block_num) {
      // elog("unlinkable block: has ${has}, got ${got}", ("has", head->head_block_num)("got", first_num));
      return;
   }
   // TODO: more check and workaround

   auto data = fc::raw::pack(bytes_data{fc::raw::pack(b)});

   app().get_io_service().post([=, self=shared_from_this()] {
      action a;
      a.name = ACTION_ADDBLOCKS;
      a.data = data;
      relay_->push_transaction(vector<action>{a}, [this, self](bool success) {
         if (success) relay_->update_local_head();
      });
   });
}

void session::on(const icp_actions& ia) {
   auto block_id = ia.block_header_instance.id();
   auto block_num = ia.block_header_instance.block_num();
   recv_transaction rt{block_num, block_id, ia.start_packet_seq, ia.start_receipt_seq};

   auto ro = relay_->get_read_only_api();
   auto r = ro.get_block(read_only::get_block_params{block_id});
   if (r.block.is_null()) { // not exist
      auto data = fc::raw::pack(bytes_data{fc::raw::pack(ia.block_header_instance)});
      action a;
      a.name = ACTION_ADDBLOCK;
      a.data = data;
      rt.action_add_block = a;
   }

   for (auto& p: ia.packet_actions) {
      auto& s = p.second;
      action a;
      a.name = s.peer_action;
      a.data = fc::raw::pack(icp_action{fc::raw::pack(s.action_instance), fc::raw::pack(s.action_receipt_instance), block_id, fc::raw::pack(ia.action_digests)});
      rt.packet_actions.emplace_back(p.first, a);
   }
   for (auto& r: ia.receipt_actions) {
      auto& s = r.second;
      action a;
      a.name = s.peer_action;
      a.data = fc::raw::pack(icp_action{fc::raw::pack(s.action_instance), fc::raw::pack(s.action_receipt_instance), block_id, fc::raw::pack(ia.action_digests)});
      rt.receipt_actions.emplace_back(r.first, a);
   }
   for (auto& c: ia.receiptend_actions) {
      action a;
      a.name = c.peer_action;
      a.data = fc::raw::pack(icp_action{fc::raw::pack(c.action_instance), fc::raw::pack(c.action_receipt_instance), block_id, fc::raw::pack(ia.action_digests)});
      rt.receiptend_actions.emplace_back(a);
   }

   app().get_io_service().post([=, self=shared_from_this()]() mutable {
      relay_->handle_icp_actions(move(rt));
   });
}

void session::on(const packet_receipt_request& req) {
   // TOOD: pre-check

   app().get_io_service().post([=, self=shared_from_this()] {
      action a;
      a.name = ACTION_GENPROOF;
      a.data = fc::raw::pack(req);
      relay_->push_transaction(vector<action>{a});
   });
}

}

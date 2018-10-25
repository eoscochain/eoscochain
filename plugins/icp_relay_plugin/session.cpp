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

void session::maybe_send_next_message() {
   verify_strand_in_this_thread(strand_, __func__, __LINE__);
   if (state_ == sending_state) return; // in process of sending
   if(out_buffer_.size()) return; // in process of sending
   if(!recv_remote_hello_ || !sent_remote_hello_) return;

   // TODO
}

void session::on_message(const icp_message& msg) {
   try {
      switch (msg.which()) {
         case icp_message::tag<hello>::value:
            on(msg.get<hello>());
            break;
         case icp_message::tag<ping>::value:
            on(msg.get<ping>());
            break;
         case icp_message::tag<pong>::value:
            on(msg.get<pong>());
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

void session::on(const hello& hi) {
   ilog("received hello: peer id ${id}, peer chain id ${chain_id}, peer icp contract ${contract}, refer to my contract ${peer_contract}", ("id", hi.id)("chain_id", hi.chain_id)("contract", hi.contract)("peer_contract", hi.peer_contract));
}

void session::on(const ping& p) {
   last_recv_ping_ = p;
   last_recv_ping_time_ = fc::time_point::now();
}

void session::on(const pong& p) {
   if (p.code != last_sent_ping_.code) {
      close();
      return;
   }
   last_sent_ping_.code = fc::sha256();
}

}

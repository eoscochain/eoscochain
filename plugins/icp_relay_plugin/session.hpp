#pragma once

#include <memory>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <fc/io/raw.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <appbase/application.hpp>

#include "message.hpp"
#include "api.hpp"

namespace icp {

using namespace std;
using tcp = boost::asio::ip::tcp;
namespace ws = boost::beast::websocket;

using namespace eosio;
using namespace eosio::chain;
using namespace appbase;

class relay; // forward declaration
using relay_ptr = std::shared_ptr<relay>;

inline void log_error(boost::system::error_code ec, const char* what) {
   elog("${w}: ${m}", ("w", what)("m", ec.message()));
}

template <typename Strand>
void verify_strand_in_this_thread(const Strand& strand, const char* func, int line) {
   if (!strand.running_in_this_thread()) {
      elog("wrong strand: ${f} : line ${n}, exiting", ("f", func)("n", line));
      app().quit();
   }
}

class session : public std::enable_shared_from_this<session> {
public:
   session(tcp::socket socket, relay_ptr relay);
   session(const string& peer, boost::asio::io_context& ioc, relay_ptr relay);
   ~session();

   void do_accept();
   void do_connect();
   void close();

   void maybe_send_next_message();
   void post(std::function<void()> callback);

   void buffer_send(icp_message&& msg);

   void update_local_head(const head& h);

   string peer_;

   int session_id_;

private:
   static int next_session_id();
   void set_socket_options();
   void on_connect(boost::system::error_code ec);
   void on_error(boost::system::error_code ec, const char* what);
   void do_hello();
   void do_read();
   void wait_on_app();
   bool send_ping();
   bool send_pong();
   void send();
   void send(const icp_message& msg);
   void on_message(const icp_message& msg);
   void check_for_redundant_connection();

   void on(const hello& hi);
   void on(const ping& p);
   void on(const pong& p);
   void on(const channel_seed& s);
   void on(const head_notice& h);
   void on(const block_header_with_merkle_path& b);
   void on(const icp_actions& ia);
   void on(const packet_receipt_request& req);

   enum session_state {
      hello_state,
      sending_state,
      idle_state
   };

   boost::asio::io_service& ios_;
   tcp::resolver resolver_;
   std::unique_ptr<ws::stream<tcp::socket>> ws_;
   boost::asio::strand<boost::asio::io_context::executor_type> strand_;

   relay_ptr relay_;

   session_state state_ = hello_state;

   string remote_host_;
   string remote_port_;

   deque<icp_message> msg_buffer_;
   vector<char> out_buffer_;
   boost::beast::flat_buffer in_buffer_;

   bool recv_remote_hello_ = false;
   bool sent_remote_hello_ = false;
   public_key_type peer_id_;

   fc::time_point last_recv_ping_time_ = fc::time_point::now();
   ping last_recv_ping_;
   ping last_sent_ping_;

   head local_head_;
};

using session_ptr = std::shared_ptr<session>;

}

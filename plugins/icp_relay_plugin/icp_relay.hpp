#pragma once

#include <memory>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <eosio/chain_plugin/chain_plugin.hpp>

#include "session.hpp"

namespace icp {

using namespace std;
using tcp = boost::asio::ip::tcp;
namespace ws = boost::beast::websocket;

using namespace eosio::chain;

class listener : public std::enable_shared_from_this<listener> {
public:
   listener(boost::asio::io_context& ioc, tcp::endpoint endpoint, relay_ptr relay);

   void do_accept();
   void on_accept(boost::system::error_code ec);

private:
   tcp::acceptor acceptor_;
   tcp::socket socket_;
   relay_ptr relay_;
};

class relay : public std::enable_shared_from_this<relay> {
public:
   void start();
   void stop();

   void start_reconnect_timer();

   void async_add_session(std::weak_ptr<session> s);
   void on_session_close(const session* s);

   void for_each_session(std::function<void (session_ptr)> callback);

   std::string endpoint_address_;
   std::uint16_t endpoint_port_;
   std::uint32_t num_threads_ = 1;
   std::vector<std::string> connect_to_peers_;

   public_key_type id_ = fc::crypto::private_key::generate().get_public_key(); // random key to identify this process
   account_name local_contract_;
   account_name peer_contract_;
   chain_id_type peer_chain_id_;

private:
   std::unique_ptr<boost::asio::io_context> ioc_;
   std::vector<std::thread> socket_threads_;
   std::shared_ptr<listener> listener_;
   std::shared_ptr<boost::asio::deadline_timer> timer_; // only access on app io_service
   std::map<const session*, std::weak_ptr<session>> sessions_; // only access on app io_service
};

}

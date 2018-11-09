#pragma once

#include <memory>

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>

#include "session.hpp"
#include "cache.hpp"
#include "api.hpp"

namespace icp {

using namespace std;
using tcp = boost::asio::ip::tcp;
namespace ws = boost::beast::websocket;

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::chain::plugin_interface;

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

   read_only get_read_only_api() { return read_only{shared_from_this()}; }
   read_write get_read_write_api() { return read_write{shared_from_this()}; }

   void start_reconnect_timer();

   void update_local_head(bool force = false);

   void async_add_session(std::weak_ptr<session> s);
   void on_session_close(const session* s);

   void for_each_session(std::function<void (session_ptr)> callback);
   void send(const icp_message& msg);

   void clear_cache_block_state();

   void open_channel(const block_header_state& seed);
   void push_transaction(vector<action> actions, function<void(bool)> callback = nullptr, packed_transaction::compression_type compression = packed_transaction::none);
   void handle_icp_actions(recv_transaction&& rt);

   std::string endpoint_address_;
   std::uint16_t endpoint_port_;
   std::uint32_t num_threads_ = 1;
   std::vector<std::string> connect_to_peers_;

   public_key_type id_ = fc::crypto::private_key::generate().get_public_key(); // random key to identify this process
   account_name local_contract_;
   account_name peer_contract_;
   chain_id_type peer_chain_id_;
   vector<chain::permission_level> signer_;
   flat_set<public_key_type> signer_required_keys_;

   head peer_head_;

   block_state_index block_states_;

private:
   void on_applied_transaction(const transaction_trace_ptr& t);
   void on_accepted_block(const block_state_with_action_digests_ptr& b);
   void on_irreversible_block(const block_state_ptr& s);
   void on_bad_block(const signed_block_ptr& b);

   void cache_block_state(block_state_ptr b);

   void push_icp_actions(const sequence_ptr& s, recv_transaction&& rt);

   void cleanup();
   // void cleanup_sequences();

   std::unique_ptr<boost::asio::io_context> ioc_;
   std::vector<std::thread> socket_threads_;
   std::shared_ptr<listener> listener_;
   std::shared_ptr<boost::asio::deadline_timer> timer_; // only access on app io_service
   std::map<const session*, std::weak_ptr<session>> sessions_; // only access on app io_service

   channels::applied_transaction::channel_type::handle on_applied_transaction_handle_;
   channels::accepted_block_with_action_digests::channel_type::handle on_accepted_block_handle_;
   channels::irreversible_block::channel_type::handle on_irreversible_block_handle_;
   channels::rejected_block::channel_type::handle on_bad_block_handle_;

   fc::microseconds tx_expiration_ = fc::seconds(30);
   uint8_t  tx_max_cpu_usage_ = 0;
   uint32_t tx_max_net_usage_ = 0;
   uint32_t delaysec_ = 0;

   fc::time_point last_transaction_time_ = fc::time_point::now();

   // uint32_t cumulative_cleanup_sequences_ = 0;
   uint32_t cumulative_cleanup_count_ = 0;

   send_transaction_index send_transactions_;
   block_with_action_digests_index block_with_action_digests_;
   recv_transaction_index recv_transactions_;
   uint32_t pending_schedule_version_ = 0;

   head local_head_;
};

}

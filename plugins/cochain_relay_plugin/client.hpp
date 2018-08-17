#pragma once

#include <memory>
#include <set>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>

#include <eosio/chain/types.hpp>
#include <appbase/application.hpp>

#include "connection.hpp"

namespace cochain {

using namespace std;

using boost::system::error_code;
using boost::asio::ip::tcp;
using boost::asio::ip::address_v4;
using boost::asio::ip::host_name;

using namespace eosio;
using namespace eosio::chain;
using namespace appbase;

constexpr auto default_max_clients = 25; // 0 for unlimited clients
constexpr auto default_max_clients_per_host = 1;
constexpr auto default_conn_cleanup_period = 30;
constexpr auto default_tx_expire_period = std::chrono::seconds(3);
constexpr auto default_response_expire_period = std::chrono::seconds(5);

class connection_status;

class client {
public:
    client();
    ~client();

    void init(const string& listen_address);
    void start();
    void stop();

    string connect(const string& host);
    string disconnect(const string& host);
    optional<connection_status> status(const string& host) const;
    vector<connection_status> connections() const;

    void start_listen_loop();
    bool start_session(connection_ptr conn);
    void close_connection(connection_ptr conn);
    void connect(connection_ptr conn);
    void connect(connection_ptr conn, tcp::resolver::iterator endpoint_iter );

    shared_ptr<tcp::resolver> resolver_;
    unique_ptr<tcp::acceptor> acceptor_;
    tcp::endpoint listen_endpoint_;
    string p2p_address_;
    string agent_name_;

    uint32_t max_clients_ = 0;
    uint32_t max_clients_per_host_ = 1;
    uint32_t num_clients_ = 0;

    vector<string> supplied_peers_;
    vector<public_key_type> allowed_peers_; // peer keys allowed to connect
    std::map<public_key_type, private_key_type> private_keys_; // overlapping with producer keys, also authenticating non-producing nodes

    enum possible_connections : char {
        None = 0,
        Producers = 1 << 0,
        Specified = 1 << 1,
        Any = 1 << 2
    };
    possible_connections allowed_connections_{None};

    std::set<connection_ptr> connections_;

    boost::asio::steady_timer::duration connection_cleanup_period_;
    boost::asio::steady_timer::duration tx_expire_period_{default_tx_expire_period};
    boost::asio::steady_timer::duration response_expire_period_{default_response_expire_period};
};

template<class enum_type, class=typename std::enable_if<std::is_enum<enum_type>::value>::type>
inline enum_type& operator|=(enum_type& lhs, const enum_type& rhs) {
    using T = std::underlying_type_t<enum_type>;
    return lhs = static_cast<enum_type>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

}

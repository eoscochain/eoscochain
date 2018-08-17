#include "cochain_relay_plugin.hpp"

#include <fc/io/json.hpp>

#include "relay.hpp"
#include "client.hpp"
#include "log.hpp"

namespace eosio {

using namespace std;
using namespace eosio;
using namespace cochain;

namespace bpo = boost::program_options;
using bpo::options_description;
using bpo::variables_map;

static appbase::abstract_plugin& _cochain_relay_plugin = app().register_plugin<cochain_relay_plugin>();

cochain_relay_plugin::cochain_relay_plugin() {}
cochain_relay_plugin::~cochain_relay_plugin() {}

void cochain_relay_plugin::set_program_options(options_description&, options_description& cfg) {
    cfg.add_options()
      ( "cochain-p2p-listen-address", bpo::value<string>()->default_value( "0.0.0.0:9876" ), "[cochain relay] The actual host:port used to listen for incoming p2p connections.")
      ( "cochain-p2p-peer-address", bpo::value<vector<string>>()->composing(), "[cochain relay] The public address of a peer node to connect to. Use multiple cochain-p2p-peer-address options as needed to compose a ICP relay network.")
      ( "cochain-p2p-max-clients", bpo::value<int>()->default_value(default_max_clients), "[cochain relay] Maximum number of clients from which connections are accepted, use 0 for no limit")
      ( "cochain-p2p-max-clients-per-host", bpo::value<int>()->default_value(default_max_clients_per_host), "[cochain relay] Maximum number of clients from any single IP address")
      ( "cochain-allowed-connection", bpo::value<vector<string>>()->multitoken()->default_value({"any"}, "any"), "[cochain relay] Can be 'any' or 'producers' or 'specified' or 'none'. If 'specified', option peer-key must be specified at least once. 'producers' and 'specified' may be combined.")
      ( "cochain-peer-key", bpo::value<vector<string>>()->composing()->multitoken(), "[cochain relay] Optional public key of peer allowed to connect. May be used multiple times.")
      ( "cochain-peer-private-key", boost::program_options::value<vector<string>>()->composing()->multitoken(),
        "[cochain relay] Tuple of [PublicKey, WIF private key] (may specify multiple times)")
      ( "cochain-agent-name", bpo::value<string>()->default_value("\"EOS Cochain Relay\""), "[cochain relay] The name supplied to identify this node amongst the peers.")
      ( "cochain-connection-cleanup-period", bpo::value<int>()->default_value(default_conn_cleanup_period), "[cochain relay] Number of seconds to wait before cleaning up dead connections")
      ( "cochain-peer-log-format", bpo::value<string>()->default_value( "[\"${_name}\" ${_ip}:${_port}]" ),
        "[cochain relay] The string used to format peers when logging messages about them. Variables are escaped with ${<variable name>}.\n"
        "Available Variables:\n"
        "   _name  \tself-reported name\n\n"
        "   _id    \tself-reported ID (64 hex characters)\n\n"
        "   _sid   \tfirst 8 characters of _peer.id\n\n"
        "   _ip    \tremote IP address of peer\n\n"
        "   _port  \tremote port number of peer\n\n"
        "   _lip   \tlocal IP address connected to peer\n\n"
        "   _lport \tlocal port number connected to peer\n\n")
      ;
}

void cochain_relay_plugin::plugin_initialize(const variables_map& options) {
    ilog("Initialize cochain relay plugin");
    cochain::peer_log_format = options.at("cochain-peer-log-format").as<string>();

    relay_->client_.connection_cleanup_period_ = std::chrono::seconds(options.at("cochain-connection-cleanup-period").as<int>());
    relay_->client_.max_clients_ = options.at("cochain-p2p-max-clients").as<uint32_t>();
    relay_->client_.max_clients_per_host_ = options.at("cochain-p2p-max-clients-per-host").as<uint32_t>();

    if (options.count("cochain-p2p-peer-address")) {
        relay_->client_.supplied_peers_ = options.at("cochain-p2p-peer-address").as<vector<string>>();
    }

    if (options.count("cochain-agent-name")) {
        relay_->client_.agent_name_ = options.at("cochain-agent-name").as<string>();
    }

    auto& allowed_connections = relay_->client_.allowed_connections_;
    const auto allowed_connection_types = options.at("cochain-allowed-connection").as<vector<string>>();
    for (const auto& conn_type: allowed_connection_types) {
        if (conn_type == "none") allowed_connections |= client::None;
        else if (conn_type == "any") allowed_connections |= client::Any;
        else if (conn_type == "producers") allowed_connections |= client::Producers;
        else if (conn_type == "specified") allowed_connections |= client::Specified;
    }
    if (allowed_connections & client::None) FC_ASSERT(allowed_connections == client::None, "cochain-allowed-connection 'none' must not accompany other options");
    if (allowed_connections & client::Any) FC_ASSERT(allowed_connections == client::Any, "cochain-allowed-connection 'any' must not accompany other options");
    if (allowed_connections & client::Specified) FC_ASSERT(options.count("cochain-peer-key"), "At least one cochain-peer-key must accompany 'cochain-allowed-connection=specified'");

    if (options.count("cochain-peer-key")) {
        const auto peer_keys = options.at("cochain-peer-key").as<vector<string>>();
        for (const auto& k: peer_keys) {
            relay_->client_.allowed_peers_.push_back(fc::json::from_string(k).as<public_key_type>());
        }
    }

    if (options.count("cochain-peer-private-key")) {
        const auto key_to_wif_str = options.at("cochain-peer-private-key").as<vector<string>>();
        for (const auto& kw: key_to_wif_str) {
            auto key_to_wif = fc::json::from_string(kw).as<pair<public_key_type, string>>();
            relay_->client_.private_keys_[key_to_wif.first] = fc::crypto::private_key(key_to_wif.second);
        }
    }

    auto listen_address = options.at("cochain-p2p-listen-address").as<string>();
    relay_->client_.init(listen_address);
}

void cochain_relay_plugin::plugin_startup() {
    configure_log();
}

void cochain_relay_plugin::plugin_shutdown() {
}

}

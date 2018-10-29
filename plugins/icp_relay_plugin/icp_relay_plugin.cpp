#include "icp_relay_plugin.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm/copy.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include "icp_relay.hpp"

namespace eosio {

using namespace std;
using namespace eosio;

namespace bpo = boost::program_options;
using bpo::options_description;
using bpo::variables_map;

using tcp = boost::asio::ip::tcp;
namespace ws  = boost::beast::websocket;

using namespace eosio::chain;

static appbase::abstract_plugin& _icp_relay_plugin = app().register_plugin<icp_relay_plugin>();

icp_relay_plugin::icp_relay_plugin() {}
icp_relay_plugin::~icp_relay_plugin() {}

void icp_relay_plugin::set_program_options(options_description&, options_description& cfg) {
    cfg.add_options()
       ("icp-relay-endpoint", bpo::value<string>()->default_value("0.0.0.0:8765"), "The endpoint upon which to listen for incoming connections")
       ("icp-relay-threads", bpo::value<uint32_t>(), "The number of threads to use to process network messages")
       ("icp-relay-connect", bpo::value<vector<string>>()->composing(), "Remote endpoint of other node to connect to (may specify multiple times)")
       ("icp-relay-peer-chain-id", bpo::value<string>(), "The chain id of icp peer")
       ("icp-relay-peer-contract", bpo::value<string>()->default_value("cochainioicp"), "The peer icp contract account name")
       ("icp-relay-local-contract", bpo::value<string>()->default_value("cochainioicp"), "The local icp contract account name")
       ("icp-relay-signer", bpo::value<string>()->default_value("cochainrelay@active"), "The account and permission level to authorize icp transactions on local icp contract, as in 'account@permission'")
    ;
}

vector<chain::permission_level> get_account_permissions(const vector<string>& permissions) {
   auto fixedPermissions = permissions | boost::adaptors::transformed([](const string& p) {
      vector<string> pieces;
      boost::algorithm::split(pieces, p, boost::algorithm::is_any_of("@"));
      if (pieces.size() == 1) pieces.push_back("active");
      return chain::permission_level{.actor = pieces[0], .permission = pieces[1]};
   });
   vector<chain::permission_level> accountPermissions;
   boost::range::copy(fixedPermissions, back_inserter(accountPermissions));
   return accountPermissions;
}

void icp_relay_plugin::plugin_initialize(const variables_map& options) {
    ilog("Initialize icp relay plugin");

    relay_ = std::make_shared<icp::relay>();

    auto endpoint = options.at("icp-relay-endpoint").as<string>();
    relay_->endpoint_address_ = endpoint.substr(0, endpoint.find(':'));
    relay_->endpoint_port_ = static_cast<uint16_t>(std::stoul(endpoint.substr(endpoint.find(':') + 1, endpoint.size())));
    ilog("icp_relay_plugin listening on ${host}:${port}", ("host", relay_->endpoint_address_)("port", relay_->endpoint_port_));

    if (options.count("icp-relay-threads")) {
        relay_->num_threads_ = options.at("icp-relay-threads").as<uint32_t>();
        if (relay_->num_threads_ > 8) relay_->num_threads_ = 8;
    }

    if (options.count("icp-relay-connect")) {
        relay_->connect_to_peers_ = options.at("icp-relay-connect").as<vector<string>>();
    }

    FC_ASSERT(options.count("icp-relay-peer-chain-id"), "option --icp-relay-peer-chain-id must be specified");
    relay_->local_contract_ = account_name(options.at("icp-relay-local-contract").as<string>());
    relay_->peer_contract_ = account_name(options.at("icp-relay-peer-contract").as<string>());
    relay_->peer_chain_id_ = chain_id_type(options.at("icp-relay-peer-chain-id").as<string>());
    relay_->signer_ = get_account_permissions(vector<string>{options.at("icp-relay-signer").as<string>()});
}

void icp_relay_plugin::plugin_startup() {
    ilog("Starting icp relay plugin");

    auto& chain = app().get_plugin<chain_plugin>().chain();
    FC_ASSERT(chain.get_read_mode() != chain::db_read_mode::IRREVERSIBLE, "icp is not compatible with \"irreversible\" read_mode");

    relay_->start();

    ilog("Started icp relay plugin");
}

void icp_relay_plugin::plugin_shutdown() {
    ilog("Stopping icp relay plugin");

    relay_->stop();

    ilog("Stopped icp relay plugin");
}

icp::read_only icp_relay_plugin::get_read_only_api() { return relay_->get_read_only_api(); }
icp::read_write icp_relay_plugin::get_read_write_api() { return relay_->get_read_write_api(); }

}

#pragma once

#include <eosio/chain/block.hpp>
#include <eosio/chain/types.hpp>

namespace cochain {

using namespace eosio::chain;
using namespace fc;

struct handshake_message {
    chain_id_type chain_id;
    string p2p_address;
};

using net_message = static_variant<handshake_message>;

FC_REFLECT(eosio::handshake_message, (chain_id))

}

/**
 *
 */

#include <fc/io/raw.hpp>

#include "api.hpp"

namespace icp {

using namespace eosio;
using namespace eosio::chain;
using namespace appbase;

const action_name ACTION_OPENCHANNEL{"openchannel"};
const action_name ACTION_ADDBLOCKS{"addblocks"};
const action_name ACTION_ADDBLOCK{"addblock"};
const action_name ACTION_SENDACTION{"sendaction"};
const action_name ACTION_ONPACKET{"onpacket"};
const action_name ACTION_ONRECEIPT{"onreceipt"};
const action_name ACTION_ONCLEANUP{"oncleanup"};
const action_name ACTION_GENPROOF{"genproof"};
const action_name ACTION_DUMMY{"dummy"};
const action_name ACTION_CLEANUP{"cleanup"};
const action_name ACTION_PRUNE{"prune"};
const action_name ACTION_ISPACKET{"ispacket"};
const action_name ACTION_ISRECEIPT{"isreceipt"};
const action_name ACTION_ISCLEANUP{"iscleanup"};

/*
struct block_header_state : chain::block_header_state {
   signature_type producer_signature;

   block_header_state() = default;
   // block_header_state(const chain::block_header_state& b) : chain::block_header_state(b) {}
   block_header_state(const chain::block_header_state& b, const signature_type& s)
      : chain::block_header_state(b), producer_signature(s) {}
   block_header_state(const chain::block_state_ptr& b)
      : chain::block_header_state(*b), producer_signature(b->block->producer_signature) {}
};
*/

struct icp_action {
   bytes action;
   bytes action_receipt;
   block_id_type block_id;
   bytes merkle_path;
};

struct bytes_data {
   bytes data;
};

struct dummy {
   account_name from;
};

struct hello {
   public_key_type id; // sender id
   chain_id_type chain_id; // sender chain id
   account_name contract; // sender contract name
   account_name peer_contract; // receiver contract name
};
struct ping {
   fc::time_point sent;
   fc::sha256 code;
   head head;
};
struct pong {
   fc::time_point sent;
   fc::sha256 code;
};
struct channel_seed {
   block_header_state seed;
};
struct block_header_with_merkle_path {
   block_header_state block_header;
   vector<block_id_type> merkle_path;
};
struct icp_actions {
   block_header block_header;
   vector<digest_type> action_digests;

   vector<action_name> peer_actions;
   vector<action> actions;
   vector<action_receipt> action_receipts;
};

using icp_message = fc::static_variant<
   hello,
   ping,
   pong,
   channel_seed,
   block_header_with_merkle_path,
   icp_actions
>;

}

FC_REFLECT(icp::hello, (id)(chain_id)(contract)(peer_contract))
FC_REFLECT(icp::ping, (sent)(code)(head))
FC_REFLECT(icp::pong, (sent)(code))
FC_REFLECT(icp::channel_seed, (seed))
FC_REFLECT(icp::block_header_with_merkle_path, (block_header)(merkle_path))
FC_REFLECT(icp::icp_actions, (block_header)(action_digests)(peer_actions)(actions)(action_receipts))

/*FC_REFLECT_DERIVED(icp::block_header_state, (eosio::chain::block_header_state), (producer_signature))*/
FC_REFLECT(icp::icp_action, (action)(action_receipt)(block_id)(merkle_path))
FC_REFLECT(icp::bytes_data, (data))
FC_REFLECT(icp::dummy, (from))

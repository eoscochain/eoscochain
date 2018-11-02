#pragma once

#include <eosio/chain/controller.hpp>

namespace icp {

using namespace eosio;
using namespace eosio::chain;

struct by_id;
struct by_num;
struct by_block_num;

typedef boost::multi_index_container<block_header_state,
   indexed_by<
      ordered_unique<tag<by_id>, member<block_header_state, block_id_type, &block_header_state::id>>,
      ordered_non_unique<tag<by_num>, member<block_header_state, uint32_t, &block_header_state::block_num>>
   >
> block_state_index;

struct send_transaction {
   transaction_id_type id;
   uint32_t block_num = 0;

   vector<action_name> peer_actions;
   vector<action> actions;
   vector<action_receipt> action_receipts;
};

typedef boost::multi_index_container<send_transaction,
   indexed_by<
      ordered_unique<tag<by_id>, member<send_transaction, transaction_id_type, &send_transaction::id>>,
      ordered_non_unique<tag<by_block_num>, member<send_transaction, uint32_t, &send_transaction::block_num>>
   >
> send_transaction_index;

struct block_with_action_digests {
   block_id_type id;
   vector<digest_type> action_digests;
};

typedef boost::multi_index_container<block_with_action_digests,
   indexed_by<
      ordered_unique<tag<by_id>, member<block_with_action_digests, block_id_type, &block_with_action_digests::id>>
   >
> block_with_action_digests_index;

}

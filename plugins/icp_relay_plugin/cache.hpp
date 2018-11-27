#pragma once

#include <eosio/chain/controller.hpp>

namespace icp {

using namespace eosio;
using namespace eosio::chain;

// TODO: configurable
constexpr uint32_t MAX_CACHED_BLOCKS = 50; // 1000
constexpr uint32_t MIN_CACHED_BLOCKS = 20; // 100
constexpr uint32_t DUMMY_ICP_SECONDS = 20; // 3600
// constexpr uint32_t MAX_CLEANUP_SEQUENCES = 3;
constexpr uint32_t MAX_CLEANUP_NUM = 10;

struct by_id;
struct by_num;
struct by_block_num;

typedef boost::multi_index_container<block_header_state,
   indexed_by<
      ordered_unique<tag<by_id>, member<block_header_state, block_id_type, &block_header_state::id>>,
      ordered_non_unique<tag<by_num>, member<block_header_state, uint32_t, &block_header_state::block_num>>
   >
> block_state_index;

struct send_transaction_internal {
   action_name peer_action;
   action action_instance;
   action_receipt action_receipt_instance;
};
struct send_transaction {
   transaction_id_type id;
   uint32_t block_num = 0;

   uint64_t start_packet_seq = 0;
   uint64_t start_receipt_seq = 0;

   std::map<uint64_t, send_transaction_internal> packet_actions; // key is packet seq
   std::map<uint64_t, send_transaction_internal> receipt_actions; // key is receipt seq
   // vector<send_transaction_internal> cleanup_actions;
   vector<send_transaction_internal> receiptend_actions;

   bool empty() const {
      return packet_actions.empty() and receipt_actions.empty() and receiptend_actions.empty();
   }
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

struct recv_transaction {
   uint32_t block_num;
   block_id_type block_id;

   uint64_t start_packet_seq;
   uint64_t start_receipt_seq;

   action action_add_block;

   vector<std::pair<uint64_t, action>> packet_actions; // key is packet seq
   vector<std::pair<uint64_t, action>> receipt_actions; // key is receipt seq
   // vector<action> cleanup_actions;
   vector<action> receiptend_actions;

   // vector<action> action_icp;
};

typedef boost::multi_index_container<recv_transaction,
   indexed_by<
      ordered_non_unique<tag<by_block_num>, member<recv_transaction, uint32_t, &recv_transaction::block_num>>
   >
> recv_transaction_index;

}

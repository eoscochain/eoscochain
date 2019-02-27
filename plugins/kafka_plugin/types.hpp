#pragma once

#include <eosio/chain/block_timestamp.hpp>

namespace kafka {

using name_t = uint64_t;
using std::string;
using bytes = std::vector<char>;
using eosio::chain::block_timestamp_type;

struct Transaction;
struct Action;

// total transaction stats
struct Stats {
   uint64_t tx_count;
   uint64_t action_count;
   uint64_t context_free_action_count;
   uint32_t max_tx_count_per_block;
   uint32_t max_action_count_per_block;
   uint32_t max_context_free_action_count_per_block;
   uint32_t account_count;
   uint32_t token_count;
};

// total producer stats
struct ProducerStats {
   name_t producer;
   uint32_t produced_blocks = 0;
   uint32_t unpaid_blocks = 0;
};

struct Block {
   bytes id;
   unsigned num;

   block_timestamp_type timestamp;

   bool lib = false; // whether irreversible

   bytes block;

   uint32_t tx_count{};
   uint32_t action_count{};
   uint32_t context_free_action_count{};

   std::vector<Transaction> transactions;
   std::vector<Action> actions;
   Stats stats;
   std::vector<ProducerStats> producer_stats;
};

struct IrreversibleBlock {
   bytes id;
   unsigned num;
};

enum TransactionStatus {
   executed, soft_fail, hard_fail, delayed, expired, unknown
};

struct Transaction {
   bytes id;

   bytes block_id;
   uint32_t block_num;
   block_timestamp_type block_time;

   uint16_t block_seq; // the sequence number of this transaction in its block

   TransactionStatus status;
   unsigned net_usage_words;
   uint32_t cpu_usage_us;

   string exception;

   uint32_t action_count{};
   uint32_t context_free_action_count{};
};

struct Action {
   uint64_t global_seq; // the global sequence number of this action
   uint64_t recv_seq; // the sequence number of this action for this receiver

   uint64_t parent_seq; // parent action trace global sequence number, only for inline traces

   name_t account; // account name
   name_t name; // action name
   bytes auth; // binary serialization of authorization array of permission_level
   bytes data; // payload

   name_t receiver; // where this action is executed on; may not be equal with `account_`, such as from notification

   bytes auth_seq;
   unsigned code_seq;
   unsigned abi_seq;

   uint32_t block_num;
   block_timestamp_type block_time;
   bytes tx_id; // the transaction that generated this action

   string console;

   string extra; // any extra data, JSON serialized
};

using BlockPtr = std::shared_ptr<Block>;
using TransactionPtr = std::shared_ptr<Transaction>;
using ActionPtr = std::shared_ptr<Action>;

}

FC_REFLECT_ENUM(kafka::TransactionStatus, (executed)(soft_fail)(hard_fail)(delayed)(expired)(unknown))
FC_REFLECT(kafka::Stats, (tx_count)(action_count)(context_free_action_count)
                         (max_tx_count_per_block)(max_action_count_per_block)(max_context_free_action_count_per_block)
                         (account_count)(token_count))
FC_REFLECT(kafka::ProducerStats, (producer)(produced_blocks)(unpaid_blocks))
FC_REFLECT(kafka::Block, (id)(num)(timestamp)(lib)(block)(tx_count)(action_count)(context_free_action_count)(transactions)(actions)(stats)(producer_stats))
FC_REFLECT(kafka::IrreversibleBlock, (id)(num))
FC_REFLECT(kafka::Transaction, (id)(block_id)(block_num)(block_time)(block_seq)(status)(net_usage_words)
                               (cpu_usage_us)(exception)(action_count)(context_free_action_count))
FC_REFLECT(kafka::Action, (global_seq)(recv_seq)(parent_seq)(account)(name)(auth)(data)(receiver)(auth_seq)
                          (code_seq)(abi_seq)(block_num)(block_time)(tx_id)(console)(extra))

#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/asset.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace eosio {

using namespace chain;

struct block_cache_object : public chainbase::object<block_cache_object_type, block_cache_object> {
   OBJECT_CTOR( block_cache_object, (block) );

   id_type id;
   block_id_type block_id;
   shared_string block;

   uint32_t block_num() const { return block_header::num_from_id(block_id); }
};

struct by_block_id;
struct by_block_num;

using block_cache_index = chainbase::shared_multi_index_container<
   block_cache_object,
   indexed_by<
      ordered_unique<tag<by_id>, member<block_cache_object, block_cache_object::id_type, &block_cache_object::id>>,
      ordered_unique<tag<by_block_id>, member<block_cache_object, block_id_type, &block_cache_object::block_id>>,
      ordered_non_unique<tag<by_block_num>, const_mem_fun<block_cache_object, uint32_t, &block_cache_object::block_num>>
   >
>;

struct stats_object : public chainbase::object<stats_object_type, stats_object> {
   OBJECT_CTOR( stats_object );

   id_type id;
   uint32_t tx_count;
   uint32_t action_count;
   uint32_t context_free_action_count;
   uint32_t max_tx_count_per_block;
   uint32_t max_action_count_per_block;
   uint32_t max_context_free_action_count_per_block;
   uint32_t account_count;
   uint32_t token_count;
};

using stats_index = chainbase::shared_multi_index_container<
   stats_object,
   indexed_by<
      ordered_unique<tag<by_id>, member<stats_object, stats_object::id_type, &stats_object::id>>
   >
>;

struct producer_stats_object : public chainbase::object<producer_stats_object_type, producer_stats_object>  {
   OBJECT_CTOR( producer_stats_object );

   id_type id;
   account_name producer;
   uint32_t produced_blocks = 0;
   uint32_t unpaid_blocks = 0;
   asset claimed_rewards;
};

struct by_producer;

using producer_stats_index = chainbase::shared_multi_index_container<
   producer_stats_object,
   indexed_by<
      ordered_unique<tag<by_id>, member<producer_stats_object, producer_stats_object::id_type, &producer_stats_object::id>>,
      ordered_unique<tag<by_producer>, member<producer_stats_object, account_name, &producer_stats_object::producer>>
   >
>;

}

CHAINBASE_SET_INDEX_TYPE(eosio::block_cache_object, eosio::block_cache_index)
CHAINBASE_SET_INDEX_TYPE(eosio::stats_object, eosio::stats_index)
CHAINBASE_SET_INDEX_TYPE(eosio::producer_stats_object, eosio::producer_stats_index)

FC_REFLECT(eosio::block_cache_object, (block_id))

namespace kafka {

using namespace std;
using namespace eosio::chain;

struct producer_schedule {
   uint32_t version = 0;
   vector<name> producers;
};

struct buyram {
   name buyer;
   name receiver;
   asset tokens;
};

struct buyrambytes {
   name buyer;
   name receiver;
   uint32_t bytes;
};

struct sellram {
   name receiver;
   int64_t bytes;
};

struct delegatebw {
   name from;
   name receiver;
   asset stake_net_quantity;
   asset stake_cpu_quantity;
   uint8_t transfer;
};

struct undelegatebw {
   name from;
   name receiver;
   asset unstake_net_quantity;
   asset unstake_cpu_quantity;
};

struct voteproducer {
   name voter;
   name proxy;
   std::vector<name> producers;
};

struct regproxy {
   name proxy;
   uint8_t isproxy;
};

struct regproducer {
   name producer;
   public_key_type producer_key;
   std::string url;
   uint16_t location;
};

struct unregprod {
   name producer;
};

struct rmvproducer {
   name producer;
};

struct claimrewards {
   name owner;
};

struct create {
   name issuer;
   asset maximum_supply;
};

struct issue {
   name to;
   asset quantity;
   string memo;
};

struct transfer {
   name from;
   name to;
   asset quantity;
   string memo;
};

struct ram_deal {
   uint64_t global_seq;
   int64_t bytes; // positive: buy; negative: sell
   asset quantity;
};

struct claimed_rewards {
   name owner;
   asset quantity;
};

struct voter_info {
   name owner;
   name proxy;
   std::vector<name> producers;
   int64_t staked;
   double last_vote_weight;
   double proxied_vote_weight;
   bool is_proxy;
   uint32_t flags1;
   uint32_t reserved2;
   asset reserved3;
};

struct producer_info {
   name owner;
   double total_votes;
   public_key_type producer_key;
   bool is_active;
   string url;
   uint32_t unpaid_blocks;
   uint64_t last_claim_time;
   uint16_t location;
};

struct producer {
   name owner;
   double total_votes;
};

struct voter {
   name owner;
   name proxy;
   int64_t staked;
   double last_vote_weight;
   double proxied_vote_weight;
   bool is_proxy;

   vector<producer> producers;
};

}

FC_REFLECT(kafka::buyram, (buyer)(receiver)(tokens))
FC_REFLECT(kafka::buyrambytes, (buyer)(receiver)(bytes))
FC_REFLECT(kafka::sellram, (receiver)(bytes))
FC_REFLECT(kafka::delegatebw, (from)(receiver)(stake_net_quantity)(stake_cpu_quantity)(transfer))
FC_REFLECT(kafka::undelegatebw, (from)(receiver)(unstake_net_quantity)(unstake_cpu_quantity))
FC_REFLECT(kafka::voteproducer, (voter)(proxy)(producers))
FC_REFLECT(kafka::regproxy, (proxy)(isproxy))
FC_REFLECT(kafka::regproducer, (producer)(producer_key)(url)(location))
FC_REFLECT(kafka::unregprod, (producer))
FC_REFLECT(kafka::rmvproducer, (producer))
FC_REFLECT(kafka::claimrewards, (owner))
FC_REFLECT(kafka::create, (issuer)(maximum_supply))
FC_REFLECT(kafka::issue, (to)(quantity)(memo))
FC_REFLECT(kafka::transfer, (from)(to)(quantity)(memo))
FC_REFLECT(kafka::ram_deal, (global_seq)(bytes)(quantity))
FC_REFLECT(kafka::claimed_rewards, (owner)(quantity))
FC_REFLECT(kafka::voter_info, (owner)(proxy)(producers)(staked)
                              (last_vote_weight)(proxied_vote_weight)
                              (is_proxy)(flags1)(reserved2)(reserved3))
FC_REFLECT(kafka::producer_info, (owner)(total_votes)(producer_key)(is_active)
                                 (url)(unpaid_blocks)(last_claim_time)(location))
FC_REFLECT(kafka::producer, (owner)(total_votes))
FC_REFLECT(kafka::voter, (owner)(proxy)(staked)(last_vote_weight)(proxied_vote_weight)
                         (is_proxy)(producers))

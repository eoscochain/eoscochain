#pragma once

#include <eosiolib/singleton.hpp>

#include "icp.hpp"

namespace eosio {

using eosio::multi_index;
using eosio::const_mem_fun;
using eosio::indexed_by;
using eosio::singleton;

key256 to_key256(const checksum256& c) {
    std::array<uint8_t, 32> a;
    std::copy(std::begin(c.hash), std::end(c.hash), a.begin());
    return key256(a);
}

using stored_block_header_ptr = std::shared_ptr<struct stored_block_header>;
using stored_block_header_state_ptr = std::shared_ptr<struct stored_block_header_state>;

/* Irreversible block header */
struct stored_block_header {
    uint64_t pk;

    block_id_type id;
    uint32_t block_num;

    checksum256 action_mroot;

    auto primary_key() const { return pk; }
    key256 by_blockid() const { return to_key256(id); }
    uint32_t by_blocknum() const { return block_num; }
};

typedef multi_index<N(block), stored_block_header,
        indexed_by<N(blockid), const_mem_fun<stored_block_header, key256, &stored_block_header::by_blockid>>,
        indexed_by<N(blocknum), const_mem_fun<stored_block_header, uint32_t, &stored_block_header::by_blocknum>>
> stored_block_header_table;

/* Block header state */
struct stored_block_header_state {
    uint64_t pk;

    block_id_type id;
    uint32_t block_num;

    incremental_merkle blockroot_merkle; // merkle root of block ids

    auto primary_key() const { return pk; }
    key256 by_blockid() const { return to_key256(id); }
    uint32_t by_blocknum() const { return block_num; }
};

typedef multi_index<N(blockstate), stored_block_header_state,
        indexed_by<N(blockid), const_mem_fun<stored_block_header_state, key256, &stored_block_header_state::by_blockid>>,
        indexed_by<N(blocknum), const_mem_fun<stored_block_header_state, uint32_t, &stored_block_header_state::by_blocknum>>
> stored_block_header_state_table;

typedef singleton<N(global), producer_schedule> producer_schedule_singleton;

using fork_store_ptr = std::shared_ptr<class fork_store>;

class fork_store {
public:
    fork_store(account_name code);

    void add_block_header(block_header);
    void add_block_header_state(block_header_state);
    bool is_producer(account_name name, ::public_key key);
    producer_schedule_ptr get_producer_schedule();
    void update_producer_schedule(const producer_schedule& schedule);
    uint32_t get_block_num(block_id_type block_id);
    // block_header_ptr get_block_header(block_id_type block_id);
    incremental_merkle_ptr get_block_mroot(block_id_type block_id);
    checksum256_ptr get_action_mroot(block_id_type block_id);

private:
    account_name code;
    stored_block_header_state_ptr head;
    producer_schedule_ptr producers;
    stored_block_header_state_table block_states;
    stored_block_header_table blocks;
};

}

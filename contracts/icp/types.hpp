#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/optional.hpp>
#include <eosiolib/producer_schedule.hpp>

#include "merkle.hpp"

bool operator==(const ::public_key& p1, const ::public_key& p2) {
    return std::equal(p1.data, p1.data + 34, p2.data, p2.data + 34);
}

bool operator!=(const ::public_key& p1, const ::public_key& p2) {
    return !std::equal(p1.data, p1.data + 34, p2.data, p2.data + 34);
}

namespace eosio {

const static int producer_repetitions = 12;

inline uint32_t endian_reverse_u32(uint32_t x) {
    return (((x >> 0x18) & 0xFF))
           | (((x >> 0x10) & 0xFF) << 0x08)
           | (((x >> 0x08) & 0xFF) << 0x10)
           | (((x) & 0xFF) << 0x18);
}

bool operator==(const producer_key& lhs, const producer_key& rhs) {
    return std::tie(lhs.producer_name, lhs.block_signing_key) == std::tie(rhs.producer_name, rhs.block_signing_key);
}

bool operator!=(const producer_key& lhs, const producer_key& rhs) {
    return std::tie(lhs.producer_name, lhs.block_signing_key) != std::tie(rhs.producer_name, rhs.block_signing_key);
}

using boost::container::flat_map;

using checksum256_ptr = std::shared_ptr<checksum256>;
using producer_schedule_ptr = std::shared_ptr<producer_schedule>;

struct block_header {
    block_timestamp_type timestamp;

    account_name producer;

    // NOTE: useless in EOS now
    uint16_t confirmed;

    block_id_type previous;

    checksum256 transaction_mroot; // merkle root of transactions
    checksum256 action_mroot; // merkle root of actions

    uint32_t schedule_version; // new version of proposed producer set
    optional<producer_schedule> new_producers; // new proposed producer set

    extensions_type header_extensions;

    static uint32_t num_from_id(const block_id_type& id);
    uint32_t block_num() const;
    block_id_type id() const;
};

using block_header_ptr = std::shared_ptr<block_header>;

struct header_confirmation {
    block_id_type block_id;
    account_name producer;
    checksum256 producer_signature;
};

struct block_header_state {
    block_id_type id;
    uint32_t block_num;

    block_header header;

    // participate in signing process
    incremental_merkle blockroot_merkle; // merkle root of block ids
    checksum256 pending_schedule_hash; // hash of producer schedule set

    // public key of producer who produced this block
    ::public_key block_signing_key;
    // signature of producer who produced this block
    signature producer_signature;

    uint32_t dpos_proposed_irreversible_blocknum; // BFT-DPOS proposed irreversible block number
    uint32_t dpos_irreversible_blocknum; // BFT-DPOS irreversible block number
    // NOTE: useless in EOS now
    uint32_t bft_irreversible_blocknum;

    uint32_t pending_schedule_lib_num; // pending irreversible block number of producers, waiting for being confirmed
    producer_schedule pending_schedule; // pending producers, waiting for being confirmed
    producer_schedule active_schedule; // current producers

    flat_map<account_name, uint32_t> producer_to_last_produced;
    flat_map<account_name, uint32_t> producer_to_last_implied_irb;

    // NOTE: useless in EOS now
    vector<uint8_t> confirm_count;
    vector<header_confirmation> confirmations;

    checksum256 sig_digest() const;
    void validate() const;

    producer_key get_scheduled_producer(block_timestamp_type t) const;
    uint32_t calc_dpos_last_irreversible() const;
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

struct block_header_with_merkle_path {
    block_header_state block_header;
    vector<block_id_type> merkle_path;

    void validate(const digest_type& root) const;
};

struct action_receipt {
    account_name receiver;
    digest_type act_digest;
    uint64_t global_sequence;
    uint64_t recv_sequence;
    flat_map<account_name, uint64_t> auth_sequence;
    uint32_t code_sequence;
    uint32_t abi_sequence;

    digest_type digest() const { return sha256(*this); }
};

// @abi table icpaction i64
struct icpaction {
    bytes action;
    bytes action_receipt;
    block_id_type block_id;
    vector<checksum256> merkle_path;
};

/* Accelerate compilation of `unpack`, other than only depends on boost::pfr.
 */

template<>
block_header unpack<block_header>( const char* buffer, size_t len ) {
    block_header bh;
    datastream<const char*> ds(buffer,len);
    ds >> bh.timestamp >> bh.producer >> bh.confirmed >> bh.previous >> bh.transaction_mroot >> bh.action_mroot >> bh.schedule_version >> bh.new_producers >> bh.header_extensions;
    return std::move(bh);
}

template<>
block_header_state unpack<block_header_state>( const char* buffer, size_t len ) {
    block_header_state h;
    block_header& bh = h.header;

    datastream<const char*> ds(buffer,len);

    ds >> h.id >> h.block_num;

    ds >> bh.timestamp >> bh.producer >> bh.confirmed >> bh.previous >> bh.transaction_mroot >> bh.action_mroot >> bh.schedule_version >> bh.new_producers >> bh.header_extensions;

    ds >> h.blockroot_merkle >> h.pending_schedule_hash >> h.block_signing_key >> h.producer_signature >> h.dpos_proposed_irreversible_blocknum >> h.dpos_irreversible_blocknum >> h.bft_irreversible_blocknum >> h.pending_schedule_lib_num >> h.pending_schedule >> h.active_schedule >> h.producer_to_last_produced >> h.producer_to_last_implied_irb >> h.confirm_count >> h.confirmations;

    return std::move(h);
}

template<>
block_header_with_merkle_path unpack<block_header_with_merkle_path>(const char* buffer, size_t len) {
    block_header_with_merkle_path m;
    block_header_state& h = m.block_header;

    datastream<const char*> ds(buffer,len);

    ds >> h.id >> h.block_num >> h.header >> h.blockroot_merkle >> h.pending_schedule_hash >> h.block_signing_key >> h.producer_signature >> h.dpos_proposed_irreversible_blocknum >> h.dpos_irreversible_blocknum >> h.bft_irreversible_blocknum >> h.pending_schedule_lib_num >> h.pending_schedule >> h.active_schedule >> h.producer_to_last_produced >> h.producer_to_last_implied_irb >> h.confirm_count >> h.confirmations;

    ds >> m.merkle_path;

    return std::move(m);
}

}

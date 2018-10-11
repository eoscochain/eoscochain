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

/* bool operator==(const checksum256& lhs, const checksum256& rhs) {
    return std::equal(std::cbegin(lhs.hash), std::cend(lhs.hash), std::cbegin(rhs.hash), std::cend(rhs.hash));
} */

bool operator!=(const checksum256& lhs, const checksum256& rhs) {
    return !std::equal(std::cbegin(lhs.hash), std::cend(lhs.hash), std::cbegin(rhs.hash), std::cend(rhs.hash));
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

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(block_header, (timestamp)(producer)(confirmed)(previous)(transaction_mroot)(action_mroot)
                                  (schedule_version)(new_producers)(header_extensions))
};

using block_header_ptr = std::shared_ptr<block_header>;

struct header_confirmation {
    block_id_type block_id;
    account_name producer;
    checksum256 producer_signature;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(header_confirmation, (block_id)(producer)(producer_signature))
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

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(block_header_state, (id)(block_num)(header)(blockroot_merkle)(pending_schedule_hash)(block_signing_key)
                                        (producer_signature)(dpos_proposed_irreversible_blocknum)(dpos_irreversible_blocknum)
                                        (bft_irreversible_blocknum)(pending_schedule_lib_num)(pending_schedule)(active_schedule)
                                        (producer_to_last_produced)(producer_to_last_implied_irb)(confirm_count)(confirmations))
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

struct block_header_with_merkle_path {
    block_header_state block_header;
    // First id must exist in `fork_store`, and the subsequent ids are linked one by one,
    // and the last one is exactly the previous id of `block_header`
    vector<block_id_type> merkle_path;

    // void validate(const digest_type& root) const;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(block_header_with_merkle_path, (block_header)(merkle_path))
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

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(action_receipt, (receiver)(act_digest)(global_sequence)(recv_sequence)(auth_sequence)(code_sequence)(abi_sequence))
};

// @abi table icp_action i64
struct icp_action {
   bytes action;
   bytes action_receipt;
   block_id_type block_id;
   vector<checksum256> merkle_path;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(icp_action, (action)(action_receipt)(block_id)(merkle_path))
};

struct [[eosio::table]] icp_packet {
    uint64_t seq; // strictly increasing sequence
    // account_name from; // the icp sender on the source chain
    // account_name to; // the icp receiver on the destination chain
    uint32_t expiration; // the expiration time in comparison to current block timestamp on destination chain, in seconds
    bytes send_action;
    bytes receipt_action; // includes `account` and `name`, but no `authorization` or `data`
    uint8_t status;

    bool shadow = false;

    uint64_t primary_key() const { return seq; }
};

enum class receipt_status : uint8_t {
    unknown = 0,
    executed = 1,
    expired = 2
    // failed = 3
};

struct [[eosio::table]] icp_receipt {
    uint64_t seq; // strictly increasing sequence
    uint64_t pseq; // sequence of the corresponding icp_packet
    // account_name from; // corresponding to the icp receiver of the icp_packet on the destination chain
    // account_name to; // corresponding to the icp sender of the icp_packet on the source chain
    uint8_t status;
    bytes data; // extra data

    bool shadow = false;

    uint64_t primary_key() const { return seq; }
    uint64_t by_pseq() const { return pseq; }
};

struct icp_cleanup {
   vector<uint64_t> seqs;
};

typedef eosio::multi_index<N(packets), icp_packet> packet_table;
typedef eosio::multi_index<N(receipts), icp_receipt,
                           indexed_by<N(pseq), const_mem_fun<icp_receipt, uint64_t, &icp_receipt::by_pseq>>
                           > receipt_table;

}

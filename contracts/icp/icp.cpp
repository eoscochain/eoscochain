#include "icp.hpp"

#include <eosiolib/crypto.h>
#include <eosiolib/public_key.hpp>

#include "hash.hpp"
#include "fork.hpp"

namespace eosio {

uint32_t block_header::num_from_id(const block_id_type& id)
{
    return endian_reverse_u32(uint32_t(*reinterpret_cast<const uint64_t*>(id.hash)));
}

uint32_t block_header::block_num() const {
    return num_from_id(previous) + 1;
}

checksum256 block_header::id() const {
    auto result = sha256(*this);
    *reinterpret_cast<uint64_t*>(result.hash) &= 0xffffffff00000000;
    *reinterpret_cast<uint64_t*>(result.hash) += endian_reverse_u32(block_num());
    return result;
}

checksum256 block_header_state::sig_digest() const {
    auto header_bmroot = sha256(std::make_pair(sha256(header), blockroot_merkle.get_root()));
    return sha256(std::make_pair(header_bmroot, pending_schedule_hash));
}

void block_header_state::validate() const {
    auto d = sig_digest();
    assert_recover_key(&d, (const char*)(producer_signature.data), 66, (const char*)(block_signing_key.data), 34);
}

producer_key block_header_state::get_scheduled_producer(block_timestamp_type t)const {
    // TODO: block_interval_ms, block_timestamp_epoch
    auto index = t.slot % (active_schedule.producers.size() * producer_repetitions);
    index /= producer_repetitions;
    return active_schedule.producers[index];
}

uint32_t block_header_state::calc_dpos_last_irreversible()const {
    vector<uint32_t> blocknums;
    blocknums.reserve(producer_to_last_implied_irb.size());
    for (auto& i : producer_to_last_implied_irb) {
        blocknums.push_back(i.second);
    }

    if (blocknums.size() == 0) return 0;
    std::sort(blocknums.begin(), blocknums.end());
    return blocknums[(blocknums.size() - 1) / 3];
}

void block_header_with_merkle_path::validate(const digest_type& root) const {
    auto merkle = block_header.blockroot_merkle;
    for (const auto& n: merkle_path) {
        merkle.append(n);
    }
    eosio_assert(merkle.get_root() == root, "invalid block merkle path");
}

icp::icp(account_name self)
        : contract(self),
          store(std::make_unique<fork_store>(self))
{}

void icp::init(const bytes& data) {
    require_auth(_self);
}

void icp::onblock(const bytes& data) {
     auto hm = unpack<block_header_with_merkle_path>(data);
    auto h = hm.block_header;

    h.validate();

    eosio_assert(store->get_block_num(h.id) == -1, "already existing block");
    eosio_assert(h.header.id() == h.id, "invalid block id");
    eosio_assert(time_point_sec(h.header.timestamp.to_time_point()) < time_point_sec(now() + 7), "received a block from the future");
    eosio_assert(store->is_producer(h.header.producer, h.block_signing_key), "invalid producer");
    auto previous_block_num = store->get_block_num(h.header.previous);
    eosio_assert(previous_block_num != -1 and previous_block_num + 1 == h.block_num, "unlinkable block");

    auto prokey = h.get_scheduled_producer(h.header.timestamp);
    eosio_assert(prokey.producer_name == h.header.producer, "invalid producer");
    eosio_assert(prokey.block_signing_key == h.block_signing_key, "invalid producer");
    eosio_assert(h.calc_dpos_last_irreversible() == h.dpos_irreversible_blocknum, "invalid dpos irreversible block num");

    if (!hm.merkle_path.empty()) {
        auto mroot = store->get_block_mroot(hm.merkle_path.back());
        eosio_assert(mroot != nullptr, "invalid block id");
        hm.validate(mroot->get_root());
    }

    if (h.header.new_producers.valid()) {
        eosio_assert(h.pending_schedule_hash == sha256(h.header.new_producers), "invalid new producers");
        eosio_assert(h.pending_schedule_lib_num == h.block_num, "invalid pending schedule lib num");
    }

    auto current_producer_schedule = store->get_producer_schedule();
    bool producer_scheduler_changed = !std::equal(h.active_schedule.producers.begin(), h.active_schedule.producers.end(), current_producer_schedule->producers.begin(), current_producer_schedule->producers.end());
    if (producer_scheduler_changed) {
        eosio_assert(h.pending_schedule_hash == sha256(h.active_schedule), "invalid new producers");
        eosio_assert(h.active_schedule.version + 1 == h.pending_schedule.version, "invalid producer schedule version");
        eosio_assert(h.dpos_irreversible_blocknum >= h.pending_schedule_lib_num, "changed producer schedule before irreversible");

        store->update_producer_schedule(h.active_schedule);
    }

    auto iter = std::find_if(h.active_schedule.producers.cbegin(), h.active_schedule.producers.cend(), [&](const producer_key& p) {
        return p.producer_name == h.header.producer;
    });
    eosio_assert(iter != h.active_schedule.producers.cend(), "invalid producer");
}

void icp::onaction(const icpaction& ia) {
    auto action_mroot = store->get_action_mroot(ia.block_id);
    eosio_assert(action_mroot != nullptr, "invalid block id");

    auto mroot = merkle(ia.merkle_path); // TODO: merkle path computation optimization
    eosio_assert(mroot == *action_mroot, "invalid actions merkle root");

    auto receipt = unpack<action_receipt>(ia.action_receipt);
    auto receipt_digest = receipt.digest();

    auto action_digest = sha256(ia.action);
    eosio_assert(action_digest == receipt.act_digest, "invalid action digest");

    bool exists = false;
    for (const auto& d: ia.merkle_path) {
        if (d == receipt_digest) {
            exists = true;
            break;
        }
    }
    eosio_assert(exists, "invalid action receipt digest");
}

}

EOSIO_ABI(eosio::icp, (init)(onblock)(onaction))

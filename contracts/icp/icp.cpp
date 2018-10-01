#include "icp.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/public_key.hpp>

#include "hash.hpp"
#include "fork.hpp"

namespace eosio {



icp::icp(account_name self)
    : contract(self),
      store(std::make_unique<fork_store>(self))
{
    peer_singleton peer(_self, _self);
    _peer = peer.get_or_default(peer_contract{})
}

void icp::setpeer(account_name peer) {
    require_auth(_self);

    peer_singleton p(_self, _self);
    eosio_assert(!p.exists(), "peer icp contract name already exists");

    p.set(peer_contract{peer}, _self);
}

void icp::setmaxpackes(uint32_t maxpackets) {

}

void icp::setmaxblocks(uint32_t maxblocks) {
    require_auth(_self);

    store->set_max_blocks(maxblocks);
}

void icp::openchannel(const bytes &data) {
    require_auth(_self);

    auto h = unpack<block_header_state>(data);
    store->init_seed_block(h);
}

void icp::closechannel() {
    require_auth(_self);

    packet_table packets(_self, _self);
    receipt_table receipts(_self, _self);
    eosio_assert(packets.begin() == packets.end(), "remain packets");
    eosio_assert(receipts.begin() == receipts.end(), "remain receipts");
    store->reset();
}

void icp::addblocks(const bytes& data) {
    auto hm = unpack<block_header_with_merkle_path>(data);
    store->add_block_header_with_merkle_path(hm.block_header, hm.merkle_path);
}

void icp::addblock(const bytes& data) {
    store->add_block_header(unpack<block_header>(data));
}

bytes icp::extract_action(const icp_action& ia) {
    eosio_assert(_peer.peer, "empty peer icp contract");

    auto action_mroot = store->get_action_mroot(ia.block_id);
    eosio_assert(action_mroot != nullptr, "invalid block id");

    auto mroot = merkle(ia.merkle_path); // TODO: merkle path computation optimization
    eosio_assert(mroot == *action_mroot, "invalid actions merkle root");

    auto receipt = unpack<action_receipt>(ia.action_receipt);
    auto receipt_digest = receipt.digest();

    auto action_digest = sha256(ia.action);
    eosio_assert(action_digest == receipt.act_digest, "invalid action digest");

    bool exists = false;
    for (const auto &d: ia.merkle_path) {
        if (d == receipt_digest) {
            exists = true;
            break;
        }
    }
    eosio_assert(exists, "invalid action receipt digest");

    store->cutdown(block_header::num_from_id(ia.block_id));

    auto a = unpack<action>(ia.action);
    eosio_assert(a.account == _peer.peer, "invalid peer icp contract");
    eosio_assert(a.name == N(null), "invalid peer icp contract action");
    return a.data;
}

void icp::onpacket(const icp_action& ia) {
    auto action_data = extract_action(ia);

    auto packet = unpack<icp_packet>(action_data);
    eosio_assert(packet.seq == _peer.last_incoming_packet_seq + 1, "invalid packet sequence");

    ++_peer.last_incoming_packet_seq;
    ++_peer.last_outgoing_receipt_seq;
    update_peer(); // update `last_outgoing_receipt_seq`

    if (packet.expiration <= now()) {
        print("icp action has expired: ", packet.expiration, " <= ", now);

        icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::expired), {}};
        action(vector<permission_level>{}, _self, N(null), receipt).send_context_free();

        return;
    }

    // inline action call
    auto a = unpack<action>(packet.send_action);
    a.authorization.emplace_back(_self, N(active));
    a.send();

    icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::executed), {}};
    action(vector<permission_level>{}, _self, N(null), receipt).send_context_free();
}

void icp::onreceipt(const icp_action& ia) {
    auto action_data = extract_action(ia);

    auto receipt = unpack<icp_receipt>(action_data);
    eosio_assert(receipt.seq == _peer.last_incoming_receipt_seq + 1, "invalid receipt sequence");

    ++_peer.last_incoming_receipt_seq;
    update_peer();

    receipt_table receipts(_self, _self);
    auto b = receipts.begin();
    auto e = receipts.rbegin();
    if (b != receipts.end() && e != receipts.rend()) {
        eosio_assert(e->seq - b->seq < 0, "exceed maximum receipts");
    }

    receipts.emplace(_self, [&](auto& r) {
        r = receipt;
    })

    packet_table packets(_self, _self);
    auto packet = packets.get(receipt.pseq);
    eosio_assert(static_cast<receipt_status>(packet.status) == receipt_status::unknown, "packet received receipt");

    auto status = static_cast<receipt_status>(receipt.status);
    eosio_assert(status == receipt_status::executed || status == receipt_status::expired, "invalid receipt status");
    packet.status = status;

    auto receipt_action = unpack<action>(packet.receipt_action);
    receipt_action.authorization.emplace_back(_self, N(active));
    receipt_action.data = pack(std::make_tuple(receipt.pseq, receipt.status, receipt.data));
    receipt_action.send();
}

void icp::oncleanup(const icp_action& ia) {
    auto action_data = extract_action(ia);

    auto erased_range = unpack<std::pair<uint64_t, uint64_t>>(action_data);
    eosio_assert(erased_range.first <= erased_range.second, "invalid range");

    receipt_table receipts(_self, _self);
    for (auto seq = erased_range.first; seq <= erased_range.second; ++seq) {
        auto it = receipts.find(seq);
        if (it != receipts.end()) {
            receipts.erase(it);
        }
    }
}

void icp::cleanup(uint64_t start_seq, uint64_t end_seq) {
    eosio_assert(_peer.peer, "empty peer icp contract");
    eosio_assert(start_seq <= end_seq, "invalid range");

    packet_table packets(_self, _self);

    // Packet with seq `end_seq` must own receipt
    auto p = packets.get(end_seq);
    eosio_assert(static_cast<receipt_status>(p.status) != receipt_status::unknown, "packet hasn't received receipt");

    std::pair<uint64_t, uint64_t> erased_range = std::make_pair(start_seq, start_seq < end_seq ? end_seq - 1 : end_seq);

    for (auto it = packets.lower_bound(start_seq); it != packets.end() && it->seq < end_seq;) {
        if (static_cast<receipt_status>(it->status) == receipt_status::unknown) {
            auto receipt_action = unpack<action>(it->receipt_action);
            receipt_action.authorization.emplace_back(_self, N(active));
            receipt_action.data = pack(std::make_tuple(it->seq, receipt_status::expired, bytes{}));
            receipt_action.send();
        }
        it = packets.erase(it); // erase it and advance to the next object
    }

    auto it = packets.upper_bound(end_seq);
    eosio_assert(it != packets.end() && it->seq == end_seq, "packet not found");
    // If no previous pending packets exists, erase the packet `end_seq`
    auto eit = it--;
    if (it == packets.end()) {
        packets.erase(eit);
        erased_range.second = end_seq;
    }

    action(vector<permission_level>{}, _self, N(null), erased_range).send_context_free();
}

void icp::prune(uint64_t receipt_start_seq, uint64_t receipt_end_seq) {
    require_auth(_self); // mutlisig authorization

    receipt_table receipts(_self, _self);
    for (auto it = receipts.lower_bound(receipt_start_seq); it != receipts.end() && it->seq <= receipt_end_seq;) {
        it = receipts.erase(it);
    }
}

void icp::sendaction(uint64_t seq, const bytes& send_action, uint32_t expiration, const bytes& receipt_action) {
    eosio_assert(_peer.peer, "empty peer icp contract");
    require_auth(_self);

    eosio_assert(seq == ++_peer.last_outgoing_packet_seq, "invalid packet sequence");
    update_peer(); // update `last_outgoing_packet_seq`

    packet_table packets(_self, _self);
    auto b = packets.begin();
    auto e = packets.rbegin();
    if (b != packets.end() && e != packets.rend()) {
        eosio_assert(e->seq - b->seq < 0, "exceed maximum packets");
    }

    icp_packet packet{seq, expiration, send_action, receipt_action};
    packets.emplace(_self, [&](auto& p) {
        p = packet;
    });

    // action `null` does not exist, so nothing will happen locally
    action(vector<permission_level>{}, _self, N(null), packet).send_context_free();
}

void icp::genproof(uint64_t packet_seq, uint64_t receipt_seq) {
    if (packet_seq > 0) {
        packet_table packets(_self, _self);
        auto packet = packets.get(packet_seq);
        packet.shadow = true;
        action(vector<permission_level>{}, _self, N(null), packet).send_context_free();
    }

    if (receipt_seq > 0) {
        receipt_table receipts(_self, _self);
        auto receipt = receipts.get(receipt_seq);
        receipt.shadow = true;
        action(vector<permission_level>{}, _self, N(null), receipt).send_context_free();
    }
}

uint64_t icp::next_packet_seq() const {
    eosio_assert(_peer.peer, "empty peer icp contract");
    return _peer.last_outgoing_packet_seq + 1;
}

void icp::update_peer() {
    peer_singleton peer(_self, _self);
    peer.set(_peer, _self);
}

}

EOSIO_ABI(eosio::icp, (init)(onblock)(onaction))

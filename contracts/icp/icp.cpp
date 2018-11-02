#include "icp.hpp"

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/public_key.hpp>

#include "hash.hpp"

#include "merkle.cpp"
#include "types.cpp"
#include "fork.cpp"

namespace eosio {

icp::icp(account_name self)
    : contract(self),
      _store(std::make_unique<fork_store>(self))
{
    peer_singleton peer(_self, _self);
    _peer = peer.get_or_default(peer_contract{});


    if (!meter_singleton(_self, _self).exists()) {
        setmaxpackes(5000 * 60 * 60 * 24); // default unfinished packets max 24 hour at 5000TPS
    }
}

void icp::setpeer(account_name peer) {
    require_auth(_self);

    peer_singleton p(_self, _self);
    eosio_assert(!p.exists(), "peer icp contract name already exists");

    p.set(peer_contract{peer}, _self);
}

void icp::setmaxpackes(uint32_t maxpackets) {
    require_auth(_self);

    meter_singleton(_self, _self).set(icp_meter{maxpackets, 0}, _self);
}

void icp::setmaxblocks(uint32_t maxblocks) {
    require_auth(_self);

    _store->set_max_blocks(maxblocks);
}

void icp::openchannel(const bytes &data) {
    require_auth(_self);

    auto h = unpack<block_header_state>(data);
    _store->init_seed_block(h);
}

void icp::closechannel() {
    require_auth(_self);

    packet_table packets(_self, _self);
    receipt_table receipts(_self, _self);
    eosio_assert(packets.begin() == packets.end(), "remain packets");
    eosio_assert(receipts.begin() == receipts.end(), "remain receipts");
    _store->reset();
}

void icp::addblocks(const bytes& data) {
    auto hm = unpack<block_header_with_merkle_path>(data);
    _store->add_block_header_with_merkle_path(hm.block_header, hm.merkle_path);
}

void icp::addblock(const bytes& data) {
    _store->add_block_header(unpack<block_header>(data));
}

void icp::sendaction(uint64_t seq, const bytes& send_action, uint32_t expiration, const bytes& receipt_action) {
   eosio_assert(_peer.peer, "empty peer icp contract");
   // NB: this permission should be authorized to application layer contract's `eosio.code` permission
   require_auth2(_self, N(sendaction));

   eosio_assert(seq == ++_peer.last_outgoing_packet_seq, "invalid outgoing packet sequence");
   update_peer(); // update `last_outgoing_packet_seq`

   packet_table packets(_self, _self);

   /* auto b = packets.begin();
   auto e = packets.rbegin();
   if (b != packets.end() && e != packets.rend()) {
       eosio_assert(e->seq - b->seq < 0, "exceed maximum packets"); // TODO
   } */

   meter_add_packets(1);

   icp_packet packet{seq, expiration, send_action, receipt_action};
   packets.emplace(_self, [&](auto& p) {
      p = packet;
   });

   // action `ispacket` does not exist, so nothing will happen locally
   action(vector<permission_level>{}, _self, N(ispacket), packet).send_context_free();
}

void icp::maybe_cutdown(const checksum256& id, incoming_type type) {
   auto old_max = _peer.max_finished_block_num();
   auto num = block_header::num_from_id(id);
   switch (type) {
      case incoming_type::packet: _peer.last_incoming_packet_block_num = num; break;
      case incoming_type::receipt: _peer.last_incoming_receipt_block_num = num; break;
      case incoming_type::cleanup: _peer.last_incoming_cleanup_block_num = num; break;
   }
   auto new_max = _peer.max_finished_block_num();
   if (new_max > old_max) {
       _store->cutdown(new_max);
   }
}

bytes icp::extract_action(const icpaction& ia) {
    eosio_assert(_peer.peer, "empty peer icp contract");

    auto action_mroot = _store->get_action_mroot(ia.block_id);

    auto merkle_path = unpack<vector<checksum256>>(ia.merkle_path);
    auto mroot = merkle(merkle_path); // TODO: merkle path computation optimization
    eosio_assert(mroot == action_mroot, "invalid actions merkle root");

    auto receipt = unpack<action_receipt>(ia.action_receipt);
    auto receipt_digest = receipt.digest();

    auto action_digest = sha256(ia.action);
    eosio_assert(action_digest == receipt.act_digest, "invalid action digest");

    bool exists = false;
    for (const auto &d: merkle_path) {
        if (d == receipt_digest) {
            exists = true;
            break;
        }
    }
    eosio_assert(exists, "invalid action receipt digest");

    auto a = unpack<action>(ia.action);
    eosio_assert(a.account == _peer.peer, "invalid peer icp contract");
    eosio_assert(a.name == N(null), "invalid peer icp contract action");
    return a.data;
}

void icp::onpacket(const icpaction& ia) {
    auto action_data = extract_action(ia);
    maybe_cutdown(ia.block_id, incoming_type::packet);

    auto packet = unpack<icp_packet>(action_data);
    eosio_assert(packet.seq == _peer.last_incoming_packet_seq + 1, "invalid incoming packet sequence"); // TODO: is the sort order necessary?

    ++_peer.last_incoming_packet_seq;
    ++_peer.last_outgoing_receipt_seq;
    update_peer(); // update `last_outgoing_receipt_seq`

    receipt_table receipts(_self, _self);

    if (packet.expiration <= now()) {
        print_f("icp action has expired: % <= now %", uint64_t(packet.expiration), uint64_t(now));

        icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::expired), {}};
        receipts.emplace(_self, [&](auto& r) {
           r = receipt;
        });
        action(vector<permission_level>{}, _self, N(isreceipt), receipt).send_context_free();

        return;
    }

    // inline action call
    // if this call fails, the only subsequent means is waiting for the packet's expiration
    auto a = unpack<action>(packet.send_action);
    a.authorization.emplace_back(_self, N(active)); // TODO
    a.send();

    // TODO: is it feasible that the inline action generate an inline context free action, which is carried as the receipt's action data?

    icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::executed), {}};
    receipts.emplace(_self, [&](auto& r) {
       r = receipt;
    });
    action(vector<permission_level>{}, _self, N(isreceipt), receipt).send_context_free();
}

void icp::onreceipt(const icpaction& ia) {
    auto action_data = extract_action(ia);
    maybe_cutdown(ia.block_id, incoming_type::receipt);

    auto receipt = unpack<icp_receipt>(action_data);
    eosio_assert(receipt.seq == _peer.last_incoming_receipt_seq + 1, "invalid receipt sequence");

    ++_peer.last_incoming_receipt_seq;
    update_peer();

    // receipt_table receipts(_self, _self);
    /* auto b = receipts.begin();
    auto e = receipts.rbegin();
    if (b != receipts.end() && e != receipts.rend()) {
        eosio_assert(e->seq - b->seq < 0, "exceed maximum receipts"); // TODO
    } */

    /* receipts.emplace(_self, [&](auto& r) {
        r = receipt;
    }); */

    packet_table packets(_self, _self);
    auto packet = packets.get(receipt.pseq, "unable find the receipt's icp_packet sequence");
    eosio_assert(static_cast<receipt_status>(packet.status) == receipt_status::unknown, "packet received receipt");

    auto status = static_cast<receipt_status>(receipt.status);
    eosio_assert(status == receipt_status::executed || status == receipt_status::expired, "invalid receipt status");

    packets.modify(packet, 0, [&](auto& p) {
        p.status = receipt.status;
    });

    if (not packet.receipt_action.empty()) {
        // this action call **cannot** fail, otherwise the icp will not proceed any more
        auto receipt_action = unpack<action>(packet.receipt_action);
        receipt_action.authorization.emplace_back(_self, N(active)); // TODO
        receipt_action.data = pack(std::make_tuple(receipt.pseq, receipt.status, receipt.data));
        receipt_action.send();
    }
}

void icp::oncleanup(const icpaction& ia) {
    auto action_data = extract_action(ia);
    maybe_cutdown(ia.block_id, incoming_type::cleanup);
    update_peer();

    auto erased_range = unpack<std::pair<uint64_t, uint64_t>>(action_data);
    eosio_assert(erased_range.first <= erased_range.second, "invalid range");

    receipt_table receipts(_self, _self);
    auto by_pseq = receipts.get_index<N(pseq)>();
    for (auto seq = erased_range.first; seq <= erased_range.second; ++seq) {
        auto it = by_pseq.find(seq);
        if (it != by_pseq.end()) {
            by_pseq.erase(it);
        }
    }
}

void icp::cleanup(uint64_t start_seq, uint64_t end_seq) {
    eosio_assert(_peer.peer, "empty peer icp contract");
    eosio_assert(start_seq <= end_seq, "invalid range");

    packet_table packets(_self, _self);

    // Packet with seq `end_seq` must own receipt
    auto p = packets.get(end_seq, "unable find icp_packet sequence");
    eosio_assert(static_cast<receipt_status>(p.status) != receipt_status::unknown, "packet hasn't received receipt");

    std::pair<uint64_t, uint64_t> erased_range = std::make_pair(start_seq, start_seq < end_seq ? end_seq - 1 : end_seq);

    uint32_t num = 0;

    for (auto it = packets.lower_bound(start_seq); it != packets.end() && it->seq < end_seq;) {
        // This condition won't happen, because receipts have sort order now. TODO: is the sort order necessary?
        if (static_cast<receipt_status>(it->status) == receipt_status::unknown and not it->receipt_action.empty()) {
            auto receipt_action = unpack<action>(it->receipt_action);
            receipt_action.authorization.emplace_back(_self, N(active));
            receipt_action.data = pack(std::make_tuple(it->seq, receipt_status::expired, bytes{}));
            receipt_action.send();
        }

        it = packets.erase(it); // erase it and advance to the next object
        ++num;
    }

    auto it = packets.upper_bound(end_seq);
    eosio_assert(it != packets.end() && it->seq == end_seq, "packet not found");
    // If no previous pending packets exists, erase the packet `end_seq`
    auto eit = it--;
    if (it == packets.end()) {
        packets.erase(eit);
        erased_range.second = end_seq;
        ++num;
    }

    meter_remove_packets(num);

    action(vector<permission_level>{}, _self, N(iscleanup), erased_range).send_context_free();
}

void icp::prune(uint64_t receipt_start_seq, uint64_t receipt_end_seq) {
    require_auth(_self); // mutlisig authorization

    receipt_table receipts(_self, _self);
    for (auto it = receipts.lower_bound(receipt_start_seq); it != receipts.end() && it->seq <= receipt_end_seq;) {
        it = receipts.erase(it);
    }
}

void icp::genproof(uint64_t packet_seq, uint64_t receipt_seq) { // TODO: rate limiting, anti spam
    if (packet_seq > 0) {
        packet_table packets(_self, _self);
        auto packet = packets.get(packet_seq, "unable find icp_packet sequence");
        packet.shadow = true;
        action(vector<permission_level>{}, _self, N(ispacket), packet).send_context_free();
    }

    if (receipt_seq > 0) {
        receipt_table receipts(_self, _self);
        auto receipt = receipts.get(receipt_seq, "unable find icp_receipt sequence");
        receipt.shadow = true;
        action(vector<permission_level>{}, _self, N(isreceipt), receipt).send_context_free();
    }
}

void icp::dummy() {
    auto seq = next_packet_seq();
    // auto icp_send = action(vector<permission_level>{}, _peer.peer, 0, bytes{});
    auto send_action = bytes{};
    auto receive_action = bytes{};
    // set expiration to 0
    INLINE_ACTION_SENDER(eosio::icp, sendaction)(_self, {_self, N(eosio.code)}, {seq, send_action, 0, receive_action});
}

uint64_t icp::next_packet_seq() const {
    eosio_assert(_peer.peer, "empty peer icp contract");
    return _peer.last_outgoing_packet_seq + 1;
}

void icp::update_peer() {
    peer_singleton peer(_self, _self);
    peer.set(_peer, _self);
}

void icp::meter_add_packets(uint32_t num) {
    if (num <= 0) return;
    meter_singleton icp_meter(_self, _self);
    auto meter = icp_meter.get();
    meter.current_packets += num;
    eosio_assert(meter.current_packets <= meter.max_packets, "exceed max packets");
    icp_meter.set(meter, _self);
}

void icp::meter_remove_packets(uint32_t num) {
    if (num <= 0) return;
    meter_singleton icp_meter(_self, _self);
    auto meter = icp_meter.get();
    meter.current_packets = meter.current_packets >= num ? meter.current_packets - num : 0;
    icp_meter.set(meter, _self);
}

}

EOSIO_ABI(eosio::icp, (setpeer)(setmaxpackes)(setmaxblocks)(openchannel)(closechannel)
                      (addblocks)(addblock)(onpacket)(onreceipt)(oncleanup)(cleanup)(sendaction)(genproof)(prune))

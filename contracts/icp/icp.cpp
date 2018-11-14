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
      meter_singleton(_self, _self).set(icp_meter{5000 * 60 * 60 * 24, 0}, _self); // default unfinished packets max 24 hour at 5000TPS
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

void icp::closechannel(uint8_t clear_all, uint32_t max_num) {
   require_auth(_self);

   if (max_num == 0) max_num = std::numeric_limits<uint32_t>::max();

   packet_table packets(_self, _self);
   receipt_table receipts(_self, _self);

   if (clear_all) { // dangerous!
      for (auto it = packets.begin(); it != packets.end();) {
         if (max_num <= 0) break; --max_num;
         it = packets.erase(it);
      }
      for (auto it = receipts.begin(); it != receipts.end();) {
         if (max_num <= 0) break; --max_num;
         it = receipts.erase(it);
      }
      peer_singleton(_self, _self).remove();
      meter_singleton(_self, _self).remove();
   }

   eosio_assert(max_num <= 0 or packets.begin() == packets.end(), "remain packets");
   eosio_assert(max_num <= 0 or receipts.begin() == receipts.end(), "remain receipts");

   _store->reset(clear_all, max_num);
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

   eosio_assert(seq == ++_peer.last_outgoing_packet_seq, ("invalid outgoing packet sequence " + std::to_string(seq)).data());
   update_peer(); // update `last_outgoing_packet_seq`

   packet_table packets(_self, _self);

   meter_add_packets(1);

   icp_packet packet{seq, expiration, send_action, receipt_action};
   packets.emplace(_self, [&](auto& p) {
      p = packet;
   });

   // action `ispacket` does not exist, so nothing will happen locally
   action(vector<permission_level>{}, _self, N(ispacket), packet).send();
}

void icp::set_last_incoming_block_num(const checksum256& id, incoming_type type) {
   auto num = block_header::num_from_id(id);
   switch (type) {
      case incoming_type::packet: _peer.last_incoming_packet_block_num = num; break;
      case incoming_type::receipt: _peer.last_incoming_receipt_block_num = num; break;
      case incoming_type::receiptend: _peer.last_incoming_receiptend_block_num = num; break;
   }
}

bytes icp::extract_action(const icpaction& ia, const action_name& name, incoming_type type) {
   eosio_assert(_peer.peer, "empty peer icp contract");

   auto action_mroot = _store->get_action_mroot(ia.block_id);

   auto merkle_path = unpack<vector<checksum256>>(ia.merkle_path);
   auto mroot = merkle(merkle_path); // TODO: merkle path computation optimization
   eosio_assert(mroot == action_mroot, "invalid actions merkle root");

   auto receipt = unpack<action_receipt>(ia.action_receipt);

   bool exists = false;
   auto receipt_digest = receipt.digest();
   for (const auto &d: merkle_path) {
      if (d == receipt_digest) {
         exists = true;
         break;
      }
   }
   eosio_assert(exists, "invalid action receipt digest");

   auto a = unpack<action>(ia.action);
   auto action_digest = sha256(a);
   eosio_assert(action_digest == receipt.act_digest, "invalid action digest");
   eosio_assert(a.account == _peer.peer, "invalid peer icp contract");
   eosio_assert(a.name == name, "invalid peer icp contract action");

   set_last_incoming_block_num(ia.block_id, type);

   return a.data;
}

void icp::onpacket(const icpaction& ia) {
   auto action_data = extract_action(ia, N(ispacket), incoming_type::packet);

   auto packet = unpack<icp_packet>(action_data);
   eosio_assert(packet.seq == _peer.last_incoming_packet_seq + 1, ("invalid incoming packet sequence " + std::to_string(packet.seq)).data()); // TODO: is the sort order necessary?

   ++_peer.last_incoming_packet_seq;
   ++_peer.last_outgoing_receipt_seq;
   update_peer(); // update `last_outgoing_receipt_seq`

   receipt_table receipts(_self, _self);

   if (packet.expiration <= now()) {
      print_f("icp action has expired: % <= now %", uint64_t(packet.expiration), uint64_t(now()));

      icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::expired), {}};
      receipts.emplace(_self, [&](auto& r) {
         r = receipt;
      });
      action(vector<permission_level>{}, _self, N(isreceipt), receipt).send();

      return;
   }

   // inline action call
   // if this call fails, the only subsequent means is waiting for the packet's expiration
   auto a = unpack<action>(packet.send_action);
   // print("onpacket: ", name{a.account}.to_string().c_str(), ", ", name{a.name}.to_string().c_str());
   a.authorization.emplace_back(a.account, N(callback)); // TODO
   a.send();

   // TODO: is it feasible that the inline action generate an inline context free action, which is carried as the receipt's action data?

   icp_receipt receipt{_peer.last_outgoing_receipt_seq, packet.seq, static_cast<uint8_t>(receipt_status::executed), {}};
   receipts.emplace(_self, [&](auto& r) {
      r = receipt;
   });
   action(vector<permission_level>{}, _self, N(isreceipt), receipt).send();
}

void icp::onreceipt(const icpaction& ia) {
   auto action_data = extract_action(ia, N(isreceipt), incoming_type::receipt);

   auto receipt = unpack<icp_receipt>(action_data);
   eosio_assert(receipt.seq == _peer.last_incoming_receipt_seq + 1, ("invalid receipt sequence " + std::to_string(receipt.seq)).data());

   ++_peer.last_incoming_receipt_seq;
   update_peer();

   packet_table packets(_self, _self);
   auto it = packets.find(receipt.pseq);
   eosio_assert(it != packets.end(), "unable find the receipt's icp_packet sequence");
   auto& packet = *it;
   eosio_assert(static_cast<receipt_status>(packet.status) == receipt_status::unknown, "packet received receipt");

   auto status = static_cast<receipt_status>(receipt.status);
   eosio_assert(status == receipt_status::executed || status == receipt_status::expired, "invalid receipt status");

   packets.modify(packet, 0, [&](auto& p) {
      p.status = receipt.status;
   });

   if (not packet.receipt_action.empty()) {
      // this action call **cannot** fail, otherwise the icp will not proceed any more
      auto receipt_action = unpack<action>(packet.receipt_action);
      // print("receipt_action: ", uint32_t(receipt.status), ", ", name{receipt_action.name}.to_string().c_str());
      receipt_action.authorization.emplace_back(receipt_action.account, N(callback)); // TODO
      receipt_action.data = pack(std::make_tuple(receipt.pseq, receipt.status, receipt.data));
      receipt_action.send();
   }

   action(vector<permission_level>{}, _self, N(isreceiptend), receipt.seq).send();
}

void icp::onreceiptend(const icpaction& ia) {
   auto action_data = extract_action(ia, N(isreceiptend), incoming_type::receiptend);

   auto seq = unpack<uint64_t>(action_data);

   _peer.last_finalised_outgoing_receipt_seq = seq;
   update_peer();
}

void icp::genproof(uint64_t packet_seq, uint64_t receipt_seq, uint8_t finalised_receipt) { // TODO: rate limiting, anti spam
   eosio_assert(_peer.peer, "empty peer icp contract");

   if (packet_seq > 0) {
      packet_table packets(_self, _self);
      auto packet = packets.get(packet_seq, "unable find icp_packet sequence");
      packet.shadow = true;
      action(vector<permission_level>{}, _self, N(ispacket), packet).send();
   }

   if (receipt_seq > 0) {
      receipt_table receipts(_self, _self);
      auto receipt = receipts.get(receipt_seq, "unable find icp_receipt sequence");
      receipt.shadow = true;
      action(vector<permission_level>{}, _self, N(isreceipt), receipt).send();
   }

   if (finalised_receipt > 0) {
      action(vector<permission_level>{}, _self, N(isreceiptend), _peer.last_incoming_receipt_seq).send();
   }
}

void icp::cleanup(uint32_t max_num) {
   eosio_assert(_peer.peer, "empty peer icp contract");

   if (max_num == 0) max_num = std::numeric_limits<uint32_t>::max();
   auto old_max_num = max_num;

   packet_table packets(_self, _self);
   uint32_t num = 0;
   for (auto it = packets.begin(); it != packets.end() and static_cast<receipt_status>(it->status) != receipt_status::unknown;) {
      if (max_num <= 0) break; --max_num;
      it = packets.erase(it);
      ++num;
   }

   meter_remove_packets(num);

   receipt_table receipts(_self, _self);
   for (auto it = receipts.begin(); it != receipts.end() and it->seq <= _peer.last_finalised_outgoing_receipt_seq;) {
      if (max_num <= 0) break; --max_num;
      it = receipts.erase(it);
   }

   auto block_num = _peer.max_finished_block_num();
   print("cutdown to block: ", block_num);
   _store->cutdown(block_num, max_num);

   eosio_assert(max_num < old_max_num, "cleanup nothing");
}

void icp::dummy(account_name from) {
   require_auth(from);
   // TODO: auth
   auto seq = next_packet_seq();
   // auto icp_send = action(vector<permission_level>{}, _peer.peer, 0, bytes{});
   auto send_action = bytes{};
   auto receive_action = bytes{};
   // set expiration to 0
   INLINE_ACTION_SENDER(eosio::icp, sendaction)(_self, {_self, N(sendaction)}, {seq, send_action, 0, receive_action});
}

uint64_t icp::next_packet_seq() const {
   return eosio::next_packet_seq(_self);
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
                      (addblocks)(addblock)(sendaction)(onpacket)(onreceipt)(onreceiptend)(cleanup)(genproof)(dummy))

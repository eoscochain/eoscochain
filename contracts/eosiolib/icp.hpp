#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>

namespace eosio {

   enum class receipt_status : uint8_t {
      unknown = 0,
      executed = 1,
      expired = 2
      // failed = 3
   };

   struct icp_sendaction {
      uint64_t seq;
      bytes send_action;
      uint32_t expiration;
      bytes receipt_action;
   };

   struct [[eosio::table]] peer_contract {
      account_name peer = 0;

      uint64_t last_outgoing_packet_seq = 0;
      uint64_t last_incoming_packet_seq = 0; // to validate
      uint64_t last_outgoing_receipt_seq = 0;
      uint64_t last_incoming_receipt_seq = 0; // to validate

      uint64_t last_finalised_outgoing_receipt_seq = 0;

      uint32_t last_incoming_packet_block_num = 0;
      uint32_t last_incoming_receipt_block_num = 0;
      uint32_t last_incoming_receiptend_block_num = 0;

      uint32_t max_finished_block_num() const {
         return std::min({last_incoming_packet_block_num, last_incoming_receipt_block_num, last_incoming_receiptend_block_num});
      }
   };

   typedef eosio::singleton<N(peer), peer_contract> peer_singleton;

   uint64_t next_packet_seq(account_name code) {
      auto peer = peer_singleton(code, code).get_or_default(peer_contract{});
      eosio_assert(peer.peer, "empty peer icp contract");
      return peer.last_outgoing_packet_seq + 1;
   }
}

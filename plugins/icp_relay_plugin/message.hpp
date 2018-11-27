#pragma once

#include <fc/io/raw.hpp>

#include "api.hpp"
#include "cache.hpp"

namespace icp {

using namespace eosio;
using namespace eosio::chain;

static const action_name ACTION_OPENCHANNEL{"openchannel"};
static const action_name ACTION_ADDBLOCKS{"addblocks"};
static const action_name ACTION_ADDBLOCK{"addblock"};
static const action_name ACTION_SENDACTION{"sendaction"};
static const action_name ACTION_ONPACKET{"onpacket"};
static const action_name ACTION_ONRECEIPT{"onreceipt"};
static const action_name ACTION_ONRECEIPTEND{"onreceiptend"};
// static const action_name ACTION_ONCLEANUP{"oncleanup"};
static const action_name ACTION_GENPROOF{"genproof"};
static const action_name ACTION_DUMMY{"dummy"};
static const action_name ACTION_CLEANUP{"cleanup"};
// static const action_name ACTION_PRUNE{"prune"};
static const action_name ACTION_ISPACKET{"ispacket"};
static const action_name ACTION_ISRECEIPT{"isreceipt"};
static const action_name ACTION_ISRECEIPTEND{"isreceiptend"};
// static const action_name ACTION_ISCLEANUP{"iscleanup"};

struct icp_action {
   bytes action;
   bytes action_receipt;
   block_id_type block_id;
   bytes merkle_path;
};

struct bytes_data {
   bytes data;
};

struct dummy {
   account_name from;
};

struct cleanup {
   uint32_t max_num;
   // uint64_t start_seq;
   // uint64_t end_seq;
};

struct icp_packet {
   uint64_t seq;
   uint32_t expiration;
   bytes send_action;
   bytes receipt_action;
   uint8_t status;
   uint8_t shadow;

   static uint64_t get_seq(const bytes& data, uint64_t& min_seq) {
      auto seq = fc::raw::unpack<icp_packet>(data).seq;
      if (min_seq == 0 or seq < min_seq) min_seq = seq;
      return seq;
   }
};

struct icp_receipt {
   uint64_t seq;
   uint64_t pseq;
   uint8_t status;
   bytes data;
   uint8_t shadow;

   static uint64_t get_seq(const bytes& data, uint64_t& min_seq) {
      auto seq = fc::raw::unpack<icp_receipt>(data).seq;
      if (min_seq == 0 or seq < min_seq) min_seq = seq;
      return seq;
   }
};

struct hello {
   public_key_type id; // sender id
   chain_id_type chain_id; // sender chain id
   account_name contract; // sender contract name
   account_name peer_contract; // receiver contract name
};
struct ping {
   fc::time_point sent;
   fc::sha256 code;
   head head_instance;
};
struct pong {
   fc::time_point sent;
   fc::sha256 code;
};
struct channel_seed {
   block_header_state seed;
};
struct head_notice {
   head head_instance;
};
struct block_header_with_merkle_path {
   block_header_state block_header;
   vector<block_id_type> merkle_path;
};
struct icp_actions {
   block_header block_header_instance;
   vector<digest_type> action_digests;

   uint64_t start_packet_seq = 0;
   uint64_t start_receipt_seq = 0;

   vector<std::pair<uint64_t, send_transaction_internal>> packet_actions; // key is packet seq
   vector<std::pair<uint64_t, send_transaction_internal>> receipt_actions; // key is receipt seq
   // vector<send_transaction_internal> cleanup_actions;
   vector<send_transaction_internal> receiptend_actions;

   void set_seq(uint64_t pseq, uint64_t rseq) {
      if (start_packet_seq == 0 or pseq < start_packet_seq) start_packet_seq = pseq;
      if (start_receipt_seq == 0 or rseq < start_receipt_seq) start_receipt_seq = rseq;
   }
};
struct packet_receipt_request {
   uint64_t packet_seq = 0;
   uint64_t receipt_seq = 0;
   uint8_t finalised_receipt = 0;

   bool empty() { return packet_seq == 0 and receipt_seq == 0 and finalised_receipt == 0; }

   friend bool operator==(const packet_receipt_request& lhs, const packet_receipt_request& rhs) {
      return lhs.packet_seq == rhs.packet_seq and lhs.receipt_seq == rhs.receipt_seq and lhs.finalised_receipt == rhs.finalised_receipt;
   }
};

using icp_message = fc::static_variant<
   hello,
   ping,
   pong,
   channel_seed,
   head_notice,
   block_header_with_merkle_path,
   icp_actions,
   packet_receipt_request
>;

}

namespace std {
   template<>
   struct hash<icp::packet_receipt_request> {
      typedef icp::packet_receipt_request argument_type;
      typedef std::size_t result_type;
      result_type operator()(argument_type const& s) const noexcept {
         return std::hash<string>{}(std::to_string(s.packet_seq) + std::to_string(s.receipt_seq) + std::to_string(s.finalised_receipt));
      }
   };
}

FC_REFLECT(icp::hello, (id)(chain_id)(contract)(peer_contract))
FC_REFLECT(icp::ping, (sent)(code)(head_instance))
FC_REFLECT(icp::pong, (sent)(code))
FC_REFLECT(icp::channel_seed, (seed))
FC_REFLECT(icp::head_notice, (head_instance))
FC_REFLECT(icp::block_header_with_merkle_path, (block_header)(merkle_path))
FC_REFLECT(icp::send_transaction_internal, (peer_action)(action_instance)(action_receipt_instance))
FC_REFLECT(icp::icp_actions, (block_header_instance)(action_digests)(start_packet_seq)(start_receipt_seq)(packet_actions)(receipt_actions)(receiptend_actions))
FC_REFLECT(icp::packet_receipt_request, (packet_seq)(receipt_seq)(finalised_receipt))

FC_REFLECT(icp::icp_action, (action)(action_receipt)(block_id)(merkle_path))
FC_REFLECT(icp::bytes_data, (data))
FC_REFLECT(icp::dummy, (from))
FC_REFLECT(icp::cleanup, (max_num))
FC_REFLECT(icp::icp_packet, (seq)(expiration)(send_action)(receipt_action)(status)(shadow))
FC_REFLECT(icp::icp_receipt, (seq)(pseq)(status)(data)(shadow))

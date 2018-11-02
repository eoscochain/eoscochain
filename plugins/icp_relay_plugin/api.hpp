#pragma once

#include <fc/static_variant.hpp>

#include <eosio/chain/types.hpp>

namespace icp {

using namespace std;
using namespace eosio::chain;

void try_catch(std::function<void()> exec, const std::string& desc = "");

class relay; // forward declaration
using relay_ptr = std::shared_ptr<relay>;

struct empty{};

struct head {
   uint32_t head_block_num = 0;
   block_id_type head_block_id;
   uint32_t last_irreversible_block_num = 0;
   block_id_type last_irreversible_block_id;

   // uint32_t active_schedule_version = 0;
   // uint32_t pending_schedule_version = 0;

   bool valid() const { return head_block_num > 0 and last_irreversible_block_num > 0; }
};

using head_ptr = std::shared_ptr<head>;

class read_only {
public:
   explicit read_only(relay_ptr relay) : relay_(std::move(relay)) {}

   head_ptr get_head() const;

   using get_info_params = empty;
   struct get_info_results {
      string icp_version;
      chain_id_type local_chain_id;
      chain_id_type peer_chain_id;
      account_name local_contract;
      account_name peer_contract;

      uint32_t head_block_num = 0;
      block_id_type head_block_id;
      uint32_t last_irreversible_block_num = 0;
      block_id_type last_irreversible_block_id;

      uint32_t max_blocks = 0;
      uint32_t current_blocks = 0;

      uint64_t last_outgoing_packet_seq = 0;
      uint64_t last_incoming_packet_seq = 0;
      uint64_t last_outgoing_receipt_seq = 0;
      uint64_t last_incoming_receipt_seq = 0;

      uint32_t max_packets = 0;
      uint32_t current_packets = 0;
   };
   get_info_results get_info(const get_info_params&) const;

private:
   relay_ptr relay_;
};

class read_write {
public:
   explicit read_write(relay_ptr relay) : relay_(relay) {}

   struct open_channel_params {
      string seed_block_num_or_id;
   };
   using open_channel_results = empty;
   // NB: the `open_channel` api can only be called once
   open_channel_results open_channel(const open_channel_params&);

private:
   relay_ptr relay_;
};

}

FC_REFLECT(icp::empty, )
FC_REFLECT(icp::head, (head_block_num)(head_block_id)(last_irreversible_block_num)(last_irreversible_block_id))
FC_REFLECT(icp::read_only::get_info_results, (icp_version)(local_chain_id)(peer_chain_id)(local_contract)(peer_contract)
                                             (head_block_num)(head_block_id)(last_irreversible_block_num)(last_irreversible_block_id)
                                             (max_blocks)(current_blocks)(last_outgoing_packet_seq)(last_incoming_packet_seq)
                                             (last_outgoing_receipt_seq)(last_incoming_receipt_seq)
                                             (max_packets)(current_packets))
FC_REFLECT(icp::read_write::open_channel_params, (seed_block_num_or_id))

#include "api.hpp"

#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include "icp_relay.hpp"

namespace icp {

using namespace appbase;
using namespace eosio;

const string ICP_VERSION = "icp-0.0.1";

std::shared_ptr<head> read_only::get_head() const {
   auto& chain = app().get_plugin<chain_plugin>();
   auto ro_api = chain.get_read_only_api();

   chain_apis::read_only::get_table_rows_params p;
   p.json = true;
   p.code = relay_->local_contract_;
   p.scope = relay_->local_contract_.to_string();
   p.table = "blockstate";
   p.limit = 1;
   p.key_type = "i128";
   p.index_position = "5"; // libblocknum
   auto blockstates = ro_api.get_table_rows(p);
   if (blockstates.rows.empty()) return nullptr;

   auto& row = blockstates.rows.front();
   std::shared_ptr<head> h = std::make_shared<head>();
   h->head_block_num = static_cast<uint32_t>(row["block_num"].as_uint64());
   h->head_block_id = block_id_type(row["id"].as_string());
   h->last_irreversible_block_num = static_cast<uint32_t>(std::max(row["dpos_irreversible_blocknum"].as_uint64(), row["bft_irreversible_blocknum"].as_uint64()));


   p.table = "block";
   p.key_type = "i64";
   p.index_position = "4"; // blocknum
   auto blocks = ro_api.get_table_rows(p); // TODO: is this right?
   if (not blocks.rows.empty()) {
      h->last_irreversible_block_id = block_id_type(blocks.rows.front()["id"].as_string());
   }

   return h;
}

read_only::get_info_results read_only::get_info(const get_info_params&) const {
   auto& chain = app().get_plugin<chain_plugin>();

   get_info_results info;
   info.icp_version = ICP_VERSION;
   info.local_chain_id = chain.get_chain_id();
   info.peer_chain_id = relay_->peer_chain_id_;
   info.local_contract = relay_->local_contract_;
   info.peer_contract = relay_->peer_contract_;

   auto ro_api = chain.get_read_only_api();

   auto head = get_head();
   if (head) {
      info.head_block_num = head->head_block_num;
      info.head_block_id = head->head_block_id;
      info.last_irreversible_block_num = head->last_irreversible_block_num;
      info.last_irreversible_block_id = head->last_irreversible_block_id;
   }

   chain_apis::read_only::get_table_rows_params p;
   p.json = true;
   p.code = relay_->local_contract_;
   p.scope = relay_->local_contract_.to_string();
   p.table = "storemeter";
   p.limit = 1;
   p.key_type = "";
   p.index_position = "";
   auto storemeter = ro_api.get_table_rows(p);
   if (not storemeter.rows.empty()) {
      auto& row = storemeter.rows.front();
      info.max_blocks = static_cast<uint32_t>(row["max_blocks"].as_uint64());
      info.current_blocks = static_cast<uint32_t>(row["current_blocks"].as_uint64());
   }

   p.table = "peer";
   auto peer = ro_api.get_table_rows(p);
   if (not peer.rows.empty()) {
      auto& row = peer.rows.front();
      info.last_outgoing_packet_seq = row["last_outgoing_packet_seq"].as_uint64();
      info.last_incoming_packet_seq = row["last_incoming_packet_seq"].as_uint64();
      info.last_outgoing_receipt_seq = row["last_outgoing_receipt_seq"].as_uint64();
      info.last_incoming_receipt_seq = row["last_incoming_receipt_seq"].as_uint64();
   }

   p.table = "icpmeter";
   auto icpmeter = ro_api.get_table_rows(p);
   if (not icpmeter.rows.empty()) {
      auto &row = peer.rows.front();
      info.max_packets = static_cast<uint32_t>(row["max_packets"].as_uint64());
      info.current_packets = static_cast<uint32_t>(row["current_packets"].as_uint64());
   }

   return info;
}

read_write::open_channel_results read_write::open_channel(const open_channel_params& params) {
   auto& chain = app().get_plugin<chain_plugin>();
   auto& controller = chain.chain();
   auto ro_api = chain.get_read_only_api();

   block_state_ptr b;
   optional<uint64_t> block_num;

   auto seed_block_num_or_id = params.seed_block_num_or_id;
   if (seed_block_num_or_id.empty()) {
      seed_block_num_or_id = controller.last_irreversible_block_id().str(); // default use the newest lib
   } else {
      try {
         block_num = fc::to_uint64(seed_block_num_or_id);
      } catch (...) {}
   }

   if( block_num.valid() ) {
      b = controller.fetch_block_state_by_number(static_cast<uint32_t>(*block_num));
   } else {
      try {
         b = controller.fetch_block_state_by_id(fc::variant(seed_block_num_or_id).as<block_id_type>());
      } EOS_RETHROW_EXCEPTIONS(chain::block_id_type_exception, "Invalid block ID: ${block_num_or_id}", ("block_num_or_id", seed_block_num_or_id))
   }

   EOS_ASSERT( b, unknown_block_exception, "Could not find reversible block: ${block}", ("block", seed_block_num_or_id));

   auto header = static_cast<const block_header_state&>(*b);

   relay_->open_channel(fc::raw::pack(header));

   open_channel_results results;
   return results;
}

}

#include "api.hpp"

#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include "icp_relay.hpp"

namespace icp {

using namespace appbase;
using namespace eosio;

const string ICP_VERSION = "icp-0.0.1";

void try_catch(std::function<void()> exec, const std::string& desc) {
   try {
      exec();
   } catch (fc::exception& e) {
      elog("FC Exception while ${desc}: ${e}", ("e", e.to_string())("desc", desc));
   } catch (std::exception& e) {
      elog("STD Exception while ${desc}: ${e}", ("e", e.what())("desc", desc));
   } catch (...) {
      elog("Unknown exception while ${desc}", ("desc", desc));
   }
}

packet_receipt_request sequence::make_genproof_request(uint64_t start_packet_seq, uint64_t start_receipt_seq) {
   packet_receipt_request req;
   if (last_incoming_packet_seq + 1 < start_packet_seq) {
      req.packet_seq = last_incoming_packet_seq + 1; // request the previous one
   }
   if (last_incoming_receipt_seq + 1 < start_receipt_seq) {
      req.receipt_seq = last_incoming_receipt_seq + 1; // request the previous one
   }
   return req;
}

head_ptr read_only::get_head() const {
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
   p.lower_bound = std::to_string(h->last_irreversible_block_num);
   p.limit = 1;
   auto blocks = ro_api.get_table_rows(p); // TODO: is this right?
   if (not blocks.rows.empty() and blocks.rows.front()["block_num"].as_uint64() == h->last_irreversible_block_num) {
      h->last_irreversible_block_id = block_id_type(blocks.rows.front()["id"].as_string());
   }

   return h;
}

sequence_ptr read_only::get_sequence(bool includes_min) const {
   auto& chain = app().get_plugin<chain_plugin>();
   auto ro_api = chain.get_read_only_api();

   chain_apis::read_only::get_table_rows_params p;
   p.json = true;
   p.code = relay_->local_contract_;
   p.scope = relay_->local_contract_.to_string();
   p.table = "peer";
   p.limit = 1;
   p.key_type = "";
   p.index_position = "";
   auto peer = ro_api.get_table_rows(p);
   if (peer.rows.empty()) return nullptr;

   auto s = std::make_shared<sequence>();

   auto& row = peer.rows.front();
   s->last_outgoing_packet_seq = row["last_outgoing_packet_seq"].as_uint64();
   s->last_incoming_packet_seq = row["last_incoming_packet_seq"].as_uint64();
   s->last_outgoing_receipt_seq = row["last_outgoing_receipt_seq"].as_uint64();
   s->last_incoming_receipt_seq = row["last_incoming_receipt_seq"].as_uint64();
   s->last_finalised_outgoing_receipt_seq = row["last_finalised_outgoing_receipt_seq"].as_uint64();
   s->last_incoming_packet_block_num = row["last_incoming_packet_block_num"].as_uint64();
   s->last_incoming_receipt_block_num = row["last_incoming_receipt_block_num"].as_uint64();
   s->last_incoming_receiptend_block_num = row["last_incoming_receiptend_block_num"].as_uint64();

   if (includes_min) {
      p.table = "packets";
      auto packets = ro_api.get_table_rows(p);
      if (not packets.rows.empty()) {
         auto& row = packets.rows.front();
         s->min_packet_seq = row["seq"].as_uint64();
      }
      p.table = "receipts";
      auto receipts = ro_api.get_table_rows(p);
      if (not receipts.rows.empty()) {
         auto& row = receipts.rows.front();
         s->min_receipt_seq = row["seq"].as_uint64();
      }
      p.table = "block";
      p.key_type = "i64";
      p.index_position = "4"; // blocknum
      auto blocks = ro_api.get_table_rows(p);
      if (not blocks.rows.empty()) {
         auto& row = blocks.rows.front();
         s->min_block_num = row["block_num"].as_uint64();
      }
   }

   return s;
}

read_only::get_block_results read_only::get_block(const get_block_params& params) {
   auto& chain = app().get_plugin<chain_plugin>();
   auto ro_api = chain.get_read_only_api();

   chain_apis::read_only::get_table_rows_params p;
   p.json = true;
   p.code = relay_->local_contract_;
   p.scope = relay_->local_contract_.to_string();
   p.table = "block";
   p.key_type = "sha256";
   p.encode_type = "hex";
   p.index_position = "2";
   p.lower_bound = params.id.str();
   p.limit = 1;
   auto blocks = ro_api.get_table_rows(p);
   if (not blocks.rows.empty() and blocks.rows.front()["id"].as<block_id_type>() == params.id) {
      return get_block_results{blocks.rows.front()};
   } else {
      return get_block_results{};
   };
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

   auto s = get_sequence();
   if (s) {
      info.last_outgoing_packet_seq = s->last_outgoing_packet_seq;
      info.last_incoming_packet_seq = s->last_incoming_packet_seq;
      info.last_outgoing_receipt_seq = s->last_outgoing_receipt_seq;
      info.last_incoming_receipt_seq = s->last_incoming_receipt_seq;
   }

   p.table = "icpmeter";
   auto icpmeter = ro_api.get_table_rows(p);
   if (not icpmeter.rows.empty()) {
      auto &row = icpmeter.rows.front();
      info.max_packets = static_cast<uint32_t>(row["max_packets"].as_uint64());
      info.current_packets = static_cast<uint32_t>(row["current_packets"].as_uint64());
   }

   return info;
}

read_write::open_channel_results read_write::open_channel(const open_channel_params& params) {
   auto& chain = app().get_plugin<chain_plugin>();
   auto& controller = chain.chain();
   auto ro_api = chain.get_read_only_api();

   std::shared_ptr<block_header_state> b;
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
      if (not b) {
         auto& idx = relay_->block_states_.get<by_num>();
         auto it = idx.find(*block_num);
         if (it != idx.end()) b = std::make_shared<block_header_state>(*it);
      }
   } else {
      try {
         b = controller.fetch_block_state_by_id(fc::variant(seed_block_num_or_id).as<block_id_type>());
         if (not b) {
            auto it = relay_->block_states_.find(fc::variant(seed_block_num_or_id).as<block_id_type>());
            if (it != relay_->block_states_.end()) b = std::make_shared<block_header_state>(*it);
         }
      } EOS_RETHROW_EXCEPTIONS(chain::block_id_type_exception, "Invalid block ID: ${block_num_or_id}", ("block_num_or_id", seed_block_num_or_id))
   }

   EOS_ASSERT( b, unknown_block_exception, "Could not find reversible block: ${block}", ("block", seed_block_num_or_id));

   auto n = block_header::num_from_id(b->id);
   auto head_num = controller.head_block_num();
   EOS_ASSERT(n + 24 <= head_num and n + 500 >= head_num, invalid_http_request, "Improper block number: ${n}", ("n", n)); // Reduce possibility of block rollback and block state prune. TODO: 24, 500 configurable?

   relay_->open_channel(*b);

   return open_channel_results{};
}

}

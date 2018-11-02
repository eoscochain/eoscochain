#include "types.hpp"

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

   digest_type block_header::digest() const {
      return sha256(*this);
   }

   checksum256 block_header_state::sig_digest() const {
      auto header_bmroot = sha256(std::make_pair(header.digest(), blockroot_merkle.get_root()));
      return sha256(std::make_pair(header_bmroot, pending_schedule_hash));
   }

   void block_header_state::validate() const {
      auto d = sig_digest();
      assert_recover_key(&d, (const char*)(header.producer_signature.data), 66, (const char*)(block_signing_key.data), 34);

      eosio_assert(header.id() == id, "invalid block id"); // TODO: necessary?
   }

   producer_key block_header_state::get_scheduled_producer(block_timestamp_type t)const {
      // TODO: block_interval_ms/block_timestamp_epoch configurable?
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

   /* void block_header_with_merkle_path::validate(const digest_type& root) const {
      auto merkle = block_header.blockroot_merkle;
      for (const auto& n: merkle_path) {
         merkle.append(n);
      }
      eosio_assert(merkle.get_root() == root, "invalid block merkle path");
   } */

}

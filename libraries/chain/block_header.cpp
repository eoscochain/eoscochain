/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#include <eosio/chain/block.hpp>
#include <eosio/chain/merkle.hpp>
#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <algorithm>

namespace eosio { namespace chain {
   digest_type block_header::digest()const
   {
      return digest_type::hash(*this);
   }

   uint32_t block_header::num_from_id(const block_id_type& id)
   {
      return fc::endian_reverse_u32(id._hash[0]);
   }

   block_id_type block_header::id()const
   {
      // Do not include signed_block_header attributes in id, specifically exclude producer_signature.
      block_id_type result = digest(); //fc::sha256::hash(*static_cast<const block_header*>(this));
      result._hash[0] &= 0xffffffff00000000;
      result._hash[0] += fc::endian_reverse_u32(block_num()); // store the block num in the ID, 160 bits is plenty for the hash
      return result;
   }

   void block_header::set_block_extensions_hash(const digest_type& hash)
   {
      header_extensions.emplace_back();
      auto& h = header_extensions.back();
      h.first = static_cast<uint16_t>(block_header_extension_type::block_extensions_hash);
      h.second.resize(hash.data_size());
      std::copy(hash.data(), hash.data() + hash.data_size(), h.second.data());
   }

} }

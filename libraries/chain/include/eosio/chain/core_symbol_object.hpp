#pragma once
#include <eosio/chain/types.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {

class core_symbol_object : public chainbase::object<core_symbol_object_type, core_symbol_object> {
   OBJECT_CTOR(core_symbol_object)

   id_type            id;
   uint64_t           core_symbol = 0;
};

using core_symbol_multi_index = chainbase::shared_multi_index_container<
   core_symbol_object,
   indexed_by<
      ordered_unique<tag<by_id>,
         BOOST_MULTI_INDEX_MEMBER(core_symbol_object, core_symbol_object::id_type, id)
      >
   >
>;

} } // eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::core_symbol_object, eosio::chain::core_symbol_multi_index)

FC_REFLECT(eosio::chain::core_symbol_object, (core_symbol))

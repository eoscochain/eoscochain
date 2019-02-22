#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/config.hpp>
#include "multi_index_includes.hpp"

namespace eosio { namespace chain {

   struct updtbwlist_params {
      uint8_t type;
      std::vector<std::string> add;
      std::vector<std::string> rmv;
   };

   /**
    * @brief Black & White list config on chain, only allows to be changed by system contract
    */
   struct blackwhitelist_config {
      blackwhitelist_config( chainbase::allocator<char> alloc )
      :  sender_bypass_whiteblacklist( alloc ),
         actor_whitelist( alloc ),
         actor_blacklist( alloc ),
         contract_whitelist( alloc ),
         contract_blacklist( alloc ),
         action_blacklist( alloc ),
         key_blacklist( alloc )
      {}

      shared_vector<account_name>   sender_bypass_whiteblacklist;
      shared_vector<account_name>   actor_whitelist;
      shared_vector<account_name>   actor_blacklist;
      shared_vector<account_name>   contract_whitelist;
      shared_vector<account_name>   contract_blacklist;
      shared_vector< pair<account_name, action_name> > action_blacklist;
      shared_vector<public_key_type> key_blacklist;
   };

   class blackwhitelist_object : public chainbase::object<blackwhitelist_object_type, blackwhitelist_object>
   {
      OBJECT_CTOR(blackwhitelist_object, (blackwhitelist))

      id_type               id;
      blackwhitelist_config blackwhitelist;
   };

   using blackwhitelist_multi_index = chainbase::shared_multi_index_container<
      blackwhitelist_object,
      indexed_by<ordered_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(blackwhitelist_object, blackwhitelist_object::id_type, id)>>
   >;
}}

FC_REFLECT(eosio::chain::updtbwlist_params, (type)(add)(rmv))

FC_REFLECT(eosio::chain::blackwhitelist_config, (sender_bypass_whiteblacklist)(actor_whitelist)(actor_blacklist)
                                                (contract_whitelist)(contract_blacklist)(action_blacklist)(key_blacklist))

CHAINBASE_SET_INDEX_TYPE(eosio::chain::blackwhitelist_object, eosio::chain::blackwhitelist_multi_index)

FC_REFLECT(eosio::chain::blackwhitelist_object, (blackwhitelist))

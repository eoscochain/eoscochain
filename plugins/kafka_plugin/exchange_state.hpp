#pragma once

#include <eosio/chain/asset.hpp>

namespace kafka {
   using namespace eosio;
   using eosio::chain::asset;
   using eosio::chain::symbol;

   typedef double real_type;

   /**
    *  Uses Bancor math to create a 50/50 relay between two asset types. The state of the
    *  bancor exchange is entirely contained within this struct. There are no external
    *  side effects associated with using this API.
    */
   struct exchange_state {
      asset    supply;

      struct connector {
         asset balance;
         double weight = .5;
      };

      connector base;
      connector quote;

      asset convert_to_exchange( connector& c, asset in );
      asset convert_from_exchange( connector& c, asset in );
      asset convert( asset from, symbol to );
   };

}

FC_REFLECT(kafka::exchange_state::connector, (balance)(weight))
FC_REFLECT(kafka::exchange_state, (supply)(base)(quote))

#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/asset.hpp>

namespace kafka {

using namespace std;
using namespace eosio::chain;

struct buyram {
   name buyer;
   name receiver;
   asset tokens;
};

struct buyrambytes {
   name buyer;
   name receiver;
   uint32_t bytes;
};

struct sellram {
   name receiver;
   int64_t bytes;
};

struct regproducer {
   name producer;
   public_key_type producer_key;
   std::string url;
   uint16_t location;
};

struct unregprod {
   name producer;
};

struct rmvproducer {
   name producer;
};

struct issue {
   name to;
   asset quantity;
   string memo;
};

struct transfer {
   name from;
   name to;
   asset quantity;
   string memo;
};

}

FC_REFLECT(kafka::buyram, (buyer)(receiver)(tokens))
FC_REFLECT(kafka::buyrambytes, (buyer)(receiver)(bytes))
FC_REFLECT(kafka::sellram, (receiver)(bytes))
FC_REFLECT(kafka::regproducer, (producer)(producer_key)(url)(location))
FC_REFLECT(kafka::unregprod, (producer))
FC_REFLECT(kafka::rmvproducer, (producer))
FC_REFLECT(kafka::issue, (to)(quantity)(memo))
FC_REFLECT(kafka::transfer, (from)(to)(quantity)(memo))

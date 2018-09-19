#pragma once

#include <memory>
#include <cstdint>

namespace eosio { namespace chain {

class random {
public:
   random( uint32_t seed );
   int operator()();

private:
   std::unique_ptr<class random_impl> impl;
};

} } // namespace eosio::chain

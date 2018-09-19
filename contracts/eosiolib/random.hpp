#pragma once

#include <random>

namespace eosio {

   std::seed_seq seed_time_and_transaction();

   std::mt19937 random_engine(std::seed_seq& seed);

}

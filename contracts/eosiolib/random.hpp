#pragma once

#include <random>

namespace eosio {

   std::seed_seq seed_timestamp_txid();
   std::seed_seq seed_timestamp_txid_signed();

   std::minstd_rand0 minstd_rand0(std::seed_seq& seed);
   std::minstd_rand minstd_rand(std::seed_seq& seed);
   std::mt19937 mt19937(std::seed_seq& seed);
   std::mt19937_64 mt19937_64(std::seed_seq& seed);
   std::ranlux24_base ranlux24_base(std::seed_seq& seed);
   std::ranlux48_base ranlux48_base(std::seed_seq& seed);
   std::ranlux24 ranlux24(std::seed_seq& seed);
   std::ranlux48 ranlux48(std::seed_seq& seed);
   std::knuth_b knuth_b(std::seed_seq& seed);

}

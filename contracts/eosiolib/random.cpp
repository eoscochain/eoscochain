#include "random.hpp"

#include "system.h"
#include "time.hpp"
#include "transaction.h"
#include "types.h"

#include "random.h"

namespace eosio {

   seed_seq_ptr seed_seq() {
      char buf[48]; // 12 * sizeof(uint32_t)
      size_t size = random_seed(buf, sizeof(buf));
      eosio_assert( size > 0 && size <= sizeof(buf), "buffer is too small" );
      uint32_t* seq = reinterpret_cast<uint32_t*>(buf);
      return std::make_unique<std::seed_seq>(seq, seq + 10);
   }

   seed_seq_ptr producer_seed_seq() {
      char buf[sizeof(signature)];
      size_t size = producer_random_seed(buf, sizeof(buf));
      eosio_assert( size > 0 && size <= sizeof(buf), "buffer is too small" );
      uint32_t* seq = reinterpret_cast<uint32_t*>(buf);
      return std::make_unique<std::seed_seq>(seq, seq + 16); // use the leading 64 bytes, discard the last 2 bytes
   }

   std::minstd_rand0 minstd_rand0(const seed_seq_ptr& seed) {
      return std::minstd_rand0(*seed);
   }
   std::minstd_rand minstd_rand(const seed_seq_ptr& seed) {
      return std::minstd_rand(*seed);
   }
   std::mt19937 mt19937(const seed_seq_ptr& seed) {
      return std::mt19937(*seed);
   }
   std::mt19937_64 mt19937_64(const seed_seq_ptr& seed) {
      return std::mt19937_64(*seed);
   }
   std::ranlux24_base ranlux24_base(const seed_seq_ptr& seed) {
      return std::ranlux24_base(*seed);
   }
   std::ranlux48_base ranlux48_base(const seed_seq_ptr& seed) {
      return std::ranlux48_base(*seed);
   }
   std::ranlux24 ranlux24(const seed_seq_ptr& seed) {
      return std::ranlux24(*seed);
   }
   std::ranlux48 ranlux48(const seed_seq_ptr& seed) {
      return std::ranlux48(*seed);
   }
   std::knuth_b knuth_b(const seed_seq_ptr& seed) {
      return std::knuth_b(*seed);
   }

}

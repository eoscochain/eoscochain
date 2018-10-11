#include "random.hpp"

#include "system.h"
#include "time.hpp"
#include "transaction.h"
#include "types.h"

/*
#define ULONG_MAX  ((unsigned long)(~0L))     // 0xFFFFFFFF for 32-bits
#define LONG_MAX   ((long)(ULONG_MAX >> 1))   // 0x7FFFFFFF for 32-bits

// x**31 + x**3 + 1.
#define	TYPE_3		3
#define	BREAK_3		128
#define	DEG_3		31
#define	SEP_3		3

#define RANDTBL_INIT TYPE_3, \
0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342, \
0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb, \
0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd, \
0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86, \
0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7, \
0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc, \
0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b, \
0xf5ad9d0e, 0x8999220b, 0x27fb47b9
*/

namespace eosio {

   /*
   static int rand_type = TYPE_3;
   static int rand_deg = DEG_3;
   static int rand_sep = SEP_3;

   class random_engine {
   public:
      random_engine( uint32_t seed );
      int operator()();

   private:
      long int randtbl[DEG_3 + 1];

      long int *fptr;
      long int *rptr;

      long int *state;

      long int *end_ptr;
   };

   random_engine::random_engine( uint32_t seed )
      : randtbl{RANDTBL_INIT},
        fptr(&randtbl[SEP_3 + 1]),
        rptr(&randtbl[1]),
        state(&randtbl[1]),
        end_ptr(&randtbl[sizeof(randtbl) / sizeof(randtbl[0])])
   {
      state[0] = seed;
      register long int i;
      for (i = 1; i < rand_deg; ++i)
         state[i] = (1103515145 * state[i - 1]) + 12345;
      fptr = &state[rand_sep];
      rptr = &state[0];
      for (i = 0; i < 10 * rand_deg; ++i) {
         this->operator()();
      }
   }

   int random_engine::operator()() {
      long int i;
      *fptr += *rptr;
      // Chucking least random bit.
      i = (*fptr >> 1) & LONG_MAX;
      ++fptr;
      if (fptr >= end_ptr)
      {
         fptr = state;
         ++rptr;
      }
      else
      {
         ++rptr;
         if (rptr >= end_ptr)
            rptr = state;
      }
      return static_cast<int>(i);
   }
   */

   std::unique_ptr<std::seed_seq> seed_timestamp_txid() {
      uint64_t current = current_time();
      uint32_t* current_halves = reinterpret_cast<uint32_t*>(&current);

      transaction_id_type tx_id;
      get_transaction_id(&tx_id);
      uint32_t* tx_id_parts = reinterpret_cast<uint32_t*>(tx_id.hash);

      return std::make_unique<std::seed_seq>({current_halves[0], current_halves[1],
                           tx_id_parts[0], tx_id_parts[1], tx_id_parts[2], tx_id_parts[3],
                           tx_id_parts[4], tx_id_parts[5], tx_id_parts[6], tx_id_parts[7]});
   }

   std::unique_ptr<std::seed_seq> seed_timestamp_txid_signed() {
      char buf[sizeof(signature)];
      size_t size = producer_random_seed(buf, sizeof(buf))
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

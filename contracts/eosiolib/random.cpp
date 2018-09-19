#include "random.hpp"

#include <cstdlib>

namespace eosio {

   void srand( uint32_t seed ) {
      std::srand(seed);
   }

   int rand() {
      return std::rand();
   }

}

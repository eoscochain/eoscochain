#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/symbol.hpp>

namespace eosio {
   namespace chain {

      uint64_t core_symbol(const string& s) {
         static uint64_t core_symbol = string_to_symbol_c(4, "SYS");
         if (!s.empty()) {
            core_symbol = string_to_symbol_c(4, s.data());
            auto cs = symbol(core_symbol);
            EOS_ASSERT(cs.name() == s, symbol_type_exception, "invalid symbol: ${s}", ("s", s));
         }
         return core_symbol;
      }

   }
}

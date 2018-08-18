#pragma once
#include <eosiolib/types.h>

extern "C" {
   uint64_t core_symbol();

   void set_core_symbol(const char* str, uint32_t len);
}

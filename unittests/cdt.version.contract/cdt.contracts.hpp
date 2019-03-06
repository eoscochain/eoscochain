#pragma once
#include <eosio/testing/tester.hpp>

namespace eosio { namespace testing {

struct contracts {
   static std::vector<uint8_t> system_wasm() { return read_wasm("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.system/eosio.system.wasm"); }
   static std::vector<char>    system_abi() { return read_abi("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.system/eosio.system.abi"); }
   static std::vector<uint8_t> token_wasm() { return read_wasm("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.token/eosio.token.wasm"); }
   static std::vector<char>    token_abi() { return read_abi("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.token/eosio.token.abi"); }
   static std::vector<uint8_t> msig_wasm() { return read_wasm("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.msig/eosio.msig.wasm"); }
   static std::vector<char>    msig_abi() { return read_abi("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.msig/eosio.msig.abi"); }
   static std::vector<uint8_t> wrap_wasm() { return read_wasm("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.wrap/eosio.wrap.wasm"); }
   static std::vector<char>    wrap_abi() { return read_abi("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.wrap/eosio.wrap.abi"); }
   static std::vector<uint8_t> bios_wasm() { return read_wasm("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.bios/eosio.bios.wasm"); }
   static std::vector<char>    bios_abi() { return read_abi("/mnt/d/Go/cpp/eoscochain/unittests/cdt.version.contract/eosio.bios/eosio.bios.abi"); }

};
}} //ns eosio::testing

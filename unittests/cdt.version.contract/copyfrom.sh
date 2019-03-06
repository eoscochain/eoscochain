#!/usr/bin/env bash

src="/mnt/d/Go/cpp/eoscochain.contracts/build"

for name in eosio.bios eosio.msig eosio.system eosio.token eosio.wrap
do
    cp "$src/$name/$name.wasm"  "./$name/$name.wasm"
    cp "$src/$name/$name.abi"  "./$name/$name.abi"
done

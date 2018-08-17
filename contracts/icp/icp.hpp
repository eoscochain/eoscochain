/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include "types.hpp"

namespace eosio {

struct icp : public contract {
    explicit icp(account_name self);

    // @abi action
    void init(const bytes& data); // initialize with a block_header_state as trust seed
    // @abi action
    void onblock(const bytes& data);
    // @abi action
    void onblockstate(const bytes& data);
    // @abi action
    void onaction(const icpaction& ia);

protected:
    std::unique_ptr<class fork_store> store;
};

}

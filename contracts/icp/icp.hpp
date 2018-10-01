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
    void setpeer(account_name peer);
    // @abi action
    void setmaxpackes(uint32_t maxpackets); // limit the maximum stored packets, to support icp rate limiting
    // @abi action
    void setmaxblocks(uint32_t maxblocks);

    // @abi action
    void openchannel(const bytes& data); // initialize with a block_header_state as trust seed
    // @abi action
    void closechannel();

    // @abi action
    void addblocks(const bytes& data);
    // @abi action
    void addblock(const bytes& data);
    // @abi action
    void onpacket(const icp_action& ia);
    // @abi action
    void onreceipt(const icp_action& ia);
    // @abi action
    void oncleanup(const icp_action& ia);
    // @abi action
    void cleanup(uint64_t start_seq, uint64_t end_seq);
    // @abi action
    void sendaction(uint64_t seq, const bytes& send_action, uint32_t expiration, const bytes& receipt_action);
    // @abi action
    void genproof(uint64_t packet_seq, uint64_t receipt_seq); // regenerate a proof of old packet/receipt
    // @abi action
    void prune(uint64_t receipt_start_seq, uint64_t receipt_end_seq); // prune oldest receipts that will not be used any more

    uint64_t next_packet_seq() const;

private:
    bytes extract_action(const icp_action& ia);
    void update_peer();

    void meter_add_packets(uint32_t num);
    void meter_remove_packets(uint32_t num = std::numeric_limits<uint32_t>::max());

    struct peer_contract {
        account_name peer = 0;
        uint64_t last_outgoing_packet_seq = 0;
        uint64_t last_incoming_packet_seq = 0; // to validate
        uint64_t last_outgoing_receipt_seq = 0;
        uint64_t last_incoming_receipt_seq = 0; // to validate
    };

    struct icp_meter {
        uint32_t max_packets;
        uint32_t current_packets;
    };

    typedef eosio::singleton<N(peer), peer_contract> peer_singleton;
    typedef eosio::singleton<N(icpmeter), icp_meter> meter_singleton;

    peer_contract _peer;
    std::unique_ptr<class fork_store> store;
};

}

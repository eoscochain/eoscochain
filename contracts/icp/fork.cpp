#include "fork.hpp"

namespace eosio {

fork_store::fork_store(account_name code) : code(code), block_states(code, code), blocks(code, code) {

}

void fork_store::add_block_header(block_header) {

}

void fork_store::add_block_header_state(block_header_state) {

}

bool fork_store::is_producer(account_name name, ::public_key key) {

}

producer_schedule_ptr fork_store::get_producer_schedule() {

}

void fork_store::update_producer_schedule(const producer_schedule& schedule) {
    producer_schedule_singleton(code, code).set(schedule, code); // TODO: payer
}

uint32_t fork_store::get_block_num(block_id_type block_id) {

}

incremental_merkle_ptr fork_store::get_block_mroot(block_id_type block_id) {

}

checksum256_ptr fork_store::get_action_mroot(block_id_type block_id) {

}

}

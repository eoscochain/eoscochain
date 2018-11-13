#include "fork.hpp"

namespace eosio {

fork_store::fork_store(account_name code)
    : _code(code),
      _block_states(code, code),
      _blocks(code, code),
      _active_schedule(code, code),
      _pending_schedule(code, code),
      _store_meter(code, code)
{
    if (!_store_meter.exists()) {
        set_max_blocks(2 * 60 * 60 + 120); // default store blocks max one hour, and add some for fork branches
    }
}

void fork_store::set_max_blocks(uint32_t max) {
    _store_meter.set(store_meter{max, 0}, _code);
}

void fork_store::validate_block_state(const block_header_state& h) {
    h.validate();

    auto by_blockid = _block_states.get_index<N(blockid)>();
    eosio_assert(by_blockid.find(to_key256(h.id)) == by_blockid.end(), "already existing block");
    eosio_assert(is_producer(h.header.producer, h.block_signing_key), "invalid producer");
    auto previous_block_num = block_header::num_from_id(h.header.previous);
    eosio_assert(previous_block_num + 1 == h.block_num, "unlinkable block");

    auto prokey = h.get_scheduled_producer(h.header.timestamp);
    eosio_assert(prokey.producer_name == h.header.producer, "invalid producer");
    eosio_assert(prokey.block_signing_key == h.block_signing_key, "invalid producer");
    auto iter = std::find_if(h.active_schedule.producers.cbegin(), h.active_schedule.producers.cend(), [&](const producer_key& p) {
       return p.producer_name == h.header.producer;
    });
    eosio_assert(iter != h.active_schedule.producers.cend(), "invalid producer");

    eosio_assert(h.calc_dpos_last_irreversible() == h.dpos_irreversible_blocknum, "invalid dpos irreversible block num");

    // If new pending schedule comes in, remember it
    if (h.header.new_producers.valid()) {
        eosio_assert(h.header.new_producers->version == h.active_schedule.version + 1, "wrong producer schedule version specified");
        eosio_assert(h.pending_schedule_hash == sha256(*h.header.new_producers), "invalid new producers");
        eosio_assert(h.pending_schedule_hash == sha256(h.pending_schedule), "invalid new producers");
        eosio_assert(h.pending_schedule_lib_num == h.block_num, "invalid pending schedule lib num");

        set_pending_schedule(h.pending_schedule_lib_num, h.pending_schedule_hash, h.pending_schedule);
    }
}

void fork_store::init_seed_block(const block_header_state& block_state) {
    eosio_assert(_block_states.begin() == _block_states.end(), "already seeded");

    update_active_schedule(block_state.active_schedule, false);
    validate_block_state(block_state);
    add_block_state(block_state);
}

void fork_store::reset(uint8_t clear_all, uint32_t max_num) {
    for (auto it = _block_states.begin(); it != _block_states.end();) {
        if (max_num <= 0) break; --max_num;
        it = _block_states.erase(it);
    }
    for (auto it = _blocks.begin(); it != _blocks.end();) {
        if (max_num <= 0) break; --max_num;
        it = _blocks.erase(it);
    }
    _active_schedule.remove();
    _pending_schedule.remove();
    meter_remove_blocks();

    if (clear_all) {
        _store_meter.remove();
    }
}

void fork_store::add_block_header_with_merkle_path(const block_header_state& h, const vector<block_id_type>& merkle_path) {
    validate_block_state(h);

    // Validate producer schedule change
    auto current_producer_schedule = get_producer_schedule();
    bool producer_scheduler_changed = (sha256(h.active_schedule) != sha256(current_producer_schedule));
    if (producer_scheduler_changed) {
        auto p = _pending_schedule.get();
        auto pending_schedule = unpack<producer_schedule>(p.pending_schedule);
        eosio_assert(sha256(h.active_schedule) == sha256(pending_schedule), "mismatched schedule");

        eosio_assert(h.pending_schedule_hash == sha256(h.active_schedule), "invalid new producers");
        eosio_assert(h.active_schedule.version + 1 == h.pending_schedule.version, "invalid producer schedule version");
        eosio_assert(h.dpos_irreversible_blocknum >= h.pending_schedule_lib_num, "changed producer schedule before irreversible");
        eosio_assert(h.dpos_irreversible_blocknum <= h.pending_schedule_lib_num + 12, "changed producer schedule too late"); // TODO: 12?

        update_active_schedule(h.active_schedule);
    }

    // To allow following block headers discontinuously, the skipped block ids should be used to compose the merkle proof
    block_id_type prev_id = merkle_path.empty() ? h.header.previous : merkle_path.front();
    auto mroot = get_block_mroot(prev_id); // first
    mroot.append(prev_id);
    if (!merkle_path.empty()) {
        auto by_blockid = _blocks.get_index<N(blockid)>();
        // TODO: acceleration optimization
        for (auto it = merkle_path.cbegin() + 1, pit = merkle_path.cbegin(); it != merkle_path.cend(); pit = it++) {
            mroot.append(*it); // intermediate
            add_block_id(by_blockid, *it, *pit);
        }
        meter_add_blocks(merkle_path.size() - 1);
    }
    // mroot.append(h.id); // last
    eosio_assert(h.blockroot_merkle.get_root() == mroot.get_root(), "unlinkable block");

    add_block_state(h);
}

void fork_store::add_block_state(const block_header_state& block_state) {
    auto by_blockid = _block_states.get_index<N(blockid)>();
    eosio_assert(by_blockid.find(to_key256(block_state.id)) == by_blockid.end(), "already existing block");

    meter_add_blocks(1);

    _block_states.emplace(_code, [&](auto& b) {
       b.pk = _block_states.available_primary_key();
       b.id = block_state.id;
       b.block_num = block_state.block_num;
       b.previous = block_state.header.previous;
       b.dpos_irreversible_blocknum = block_state.dpos_irreversible_blocknum;
       b.bft_irreversible_blocknum = block_state.bft_irreversible_blocknum;
       b.blockroot_merkle = pack(block_state.blockroot_merkle);
    });

    _blocks.emplace(_code, [&](auto& b) {
        b.pk = _blocks.available_primary_key();
        b.id = block_state.id;
        b.block_num = block_state.block_num;
        b.previous = block_state.header.previous;
        b.action_mroot = block_state.header.action_mroot;
    });

    auto head = *_block_states.get_index<N(libblocknum)>().begin();

    auto lib = head.dpos_irreversible_blocknum; // last irreversible block
    auto oldest = *_block_states.get_index<N(blocknum)>().begin();
    if (oldest.block_num < lib) {
        prune(oldest);
    }
}

void fork_store::prune(const stored_block_header_state& block_state) {
    auto num = block_state.block_num;

    auto by_blocknum = _block_states.get_index<N(blocknum)>();
    for (auto it = by_blocknum.begin(); it != by_blocknum.end() && it->block_num < num;) {
        prune(*it); // prune lower number block firstly
        it = by_blocknum.begin();
    }

    /* Note: Comment the erasing operation, to reserve the LIB blocks until icp actions in them are completely handled
    auto by_blockid = _block_states.get_index<N(blockid)>();
    auto it = by_blockid.find(block_state.id);
    if (it != by_blockid.end()) { // irreversible block
        by_blockid.erase(it);
    }
    */

    by_blocknum = _block_states.get_index<N(blocknum)>();
    for (auto it = by_blocknum.lower_bound(num); it != by_blocknum.end() && it->block_num == num;) {
        if (it->id == block_state.id) { // bypass myself
            ++it;
            continue;
        }
        auto id = it->id;
        ++it;
        remove(id);
    }
}

void fork_store::cutdown(uint32_t block_num, uint32_t& max_num) {
    auto head = *_block_states.get_index<N(libblocknum)>().begin();
    auto lib = head.last_irreversible_blocknum();
    eosio_assert(block_num <= lib, "block number not irreversible");
    if (block_num == lib) block_num = lib - 1; // retain the lib for query convenience

    {
        auto by_blocknum = _block_states.get_index<N(blocknum)>();
        for (auto it = by_blocknum.begin(); it != by_blocknum.end() && it->block_num <= block_num;) {
            if (max_num <= 0) break; --max_num;
            by_blocknum.erase(it);
            it = by_blocknum.begin();
        }
    }
    {
        uint32_t num = 0;
        auto by_blocknum = _blocks.get_index<N(blocknum)>();
        for (auto it = by_blocknum.begin(); it != by_blocknum.end() && it->block_num <= block_num;) {
            if (max_num <= 0) break; --max_num;
            by_blocknum.erase(it);
            it = by_blocknum.begin();
            ++num;
        }
        meter_remove_blocks(num);
    }
}

// Remove specified block and all its successive blocks
void fork_store::remove(const block_id_type& id) {
    vector<key256> remove_queue{to_key256(id)};
    uint32_t num = 0;

    for( uint32_t i = 0; i < remove_queue.size(); ++i ) {
        {
            auto by_blockid = _block_states.get_index<N(blockid)>();
            auto it = by_blockid.find(remove_queue[i]);
            if (it != by_blockid.end()) {
                by_blockid.erase(it);
            }
        }

        {
            auto by_blockid = _blocks.get_index<N(blockid)>();
            auto it = by_blockid.find(remove_queue[i]);
            if (it != by_blockid.end()) {
                by_blockid.erase(it);
                ++num;
            }
        }

        {
            auto by_prev = _block_states.get_index<N(prev)>();
            for (auto it = by_prev.lower_bound(remove_queue[i]); it != by_prev.end() && to_key256(it->previous) == remove_queue[i]; ++it) {
                remove_queue.push_back(to_key256(it->id));
            }
        }

        {
            auto by_prev = _blocks.get_index<N(prev)>();
            for (auto it = by_prev.lower_bound(remove_queue[i]); it != by_prev.end() && to_key256(it->previous) == remove_queue[i]; ++it) {
                remove_queue.push_back(to_key256(it->id));
            }
        }
    }

    meter_remove_blocks(num);
}

void fork_store::add_block_header(const block_header& h) {
    auto by_blockid = _blocks.get_index<N(blockid)>();
    auto b = by_blockid.find(to_key256(h.id()));
    eosio_assert(b != by_blockid.end(), "missing block");
    eosio_assert(!b->has_action_mroot(), "already complete block");
    by_blockid.modify(b, 0, [&](auto& o) {
        o.action_mroot = h.action_mroot; // TODO: assignment
    });
}

/* void fork_store::add_block_id(const block_id_type& block_id, const block_id_type& previous) {
    auto by_blockid = _blocks.get_index<N(blockid)>();
    eosio_assert(by_blockid.find(to_key256(block_id)) == by_blockid.end(), "already existing block");

    _blocks.emplace(_code, [&](auto& o) {
        o.pk = _blocks.available_primary_key();
        o.id = block_id;
        o.block_num = block_header::num_from_id(block_id);
        o.previous = previous;
        // absent `action_mroot`
    });
} */

bool fork_store::is_producer(account_name name, const ::public_key& key) {
    auto schedule = unpack<producer_schedule>(_active_schedule.get().producer_schedule);
    for (auto& p: schedule.producers) {
        if (p.producer_name == name && p.block_signing_key == key) {
           return true;
        }
    }
    return false;
}

producer_schedule fork_store::get_producer_schedule() {
    return unpack<producer_schedule>(_active_schedule.get().producer_schedule);
}

void fork_store::update_active_schedule(const producer_schedule &schedule, bool clear_pending) {
    _active_schedule.set(stored_producer_schedule{pack(schedule)}, _code);

    if (clear_pending) {
        auto s = _pending_schedule.get();
        auto pending_schedule = unpack<producer_schedule>(s.pending_schedule);
        pending_schedule.producers.clear(); // clear producers, same as in the call `maybe_promote_pending()`
        s.pending_schedule = pack(pending_schedule);
        _pending_schedule.set(s, _code);
    }
}

void fork_store::set_pending_schedule(uint32_t lib_num, const digest_type& hash, const producer_schedule& schedule) {
    auto s = pending_schedule{lib_num, hash, pack(schedule)};
    _pending_schedule.set(s, _code);
}

incremental_merkle fork_store::get_block_mroot(const block_id_type& block_id) {
    auto by_blockid = _block_states.get_index<N(blockid)>();
    auto b = by_blockid.get(to_key256(block_id), "by_blockid unable to get");
    return unpack<incremental_merkle>(b.blockroot_merkle);
}

checksum256 fork_store::get_action_mroot(const block_id_type& block_id) {
    auto by_blockid = _blocks.get_index<N(blockid)>();
    auto b = by_blockid.get(to_key256(block_id), "by_blockid unable to get");
    eosio_assert(b.has_action_mroot(), "incomplete block");

    auto head = *_block_states.get_index<N(libblocknum)>().begin();
    eosio_assert(b.block_num <= head.last_irreversible_blocknum(), "block number not irreversible");

    return b.action_mroot;
}

void fork_store::meter_add_blocks(uint32_t num) {
    if (num <= 0) return;
    auto meter = _store_meter.get();
    meter.current_blocks += num;
    eosio_assert(meter.current_blocks <= meter.max_blocks, "exceed max blocks");
    _store_meter.set(meter, _code);
}

void fork_store::meter_remove_blocks(uint32_t num) {
    if (num <= 0) return;
    auto meter = _store_meter.get();
    meter.current_blocks = meter.current_blocks >= num ? meter.current_blocks - num : 0;
    _store_meter.set(meter, _code);
}

}

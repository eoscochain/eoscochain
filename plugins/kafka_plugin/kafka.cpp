#include "kafka.hpp"

#include <eosio/chain/config.hpp>
#include <fc/io/json.hpp>

#include "try_handle.hpp"
#include "actions.hpp"
#include "exchange_state.hpp"

namespace std {
template<> struct hash<kafka::bytes> {
    typedef kafka::bytes argument_type;
    typedef size_t result_type;
    result_type operator()(argument_type const& s) const noexcept {
        return std::hash<string>{}(string(s.begin(), s.end()));
    }
};
}

namespace kafka {

using chain::account_name;
using chain::action_name;
using chain::block_id_type;
using chain::permission_name;
using chain::transaction;
using chain::signed_transaction;
using chain::signed_block;
using chain::transaction_id_type;

namespace {

inline bytes checksum_bytes(const fc::sha256& s) { return bytes(s.data(), s.data() + sizeof(fc::sha256)); }

TransactionStatus transactionStatus(fc::enum_type<uint8_t, chain::transaction_receipt::status_enum> status) {
    if (status == chain::transaction_receipt::executed) return TransactionStatus::executed;
    else if (status == chain::transaction_receipt::soft_fail) return TransactionStatus::soft_fail;
    else if (status == chain::transaction_receipt::hard_fail) return TransactionStatus::hard_fail;
    else if (status == chain::transaction_receipt::delayed) return TransactionStatus::delayed;
    else if (status == chain::transaction_receipt::expired) return TransactionStatus::expired;
    else return TransactionStatus::unknown;
}

}

void kafka::set_config(Configuration config) {
    config_ = config;
}

void kafka::set_topic(const string& topic) {
    topic_ = topic;
}

void kafka::set_partition(int partition) {
    partition_ =  partition;
}

void kafka::set_poll_interval(unsigned interval) {
    poll_interval_ = interval;
}

void kafka::start() {
    /*
    config_.set_error_callback([&](KafkaHandleBase& handle, int error, const std::string& reason) {
       if (error == RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN) {
           elog("error_callback: err ${error} ${reason}", ("error", error)("reason", reason));
       }
    });

    config_.set_delivery_report_callback([&](Producer& producer, const Message& msg) {
        auto err = msg.get_error();
        if (bool(err)) {
            const auto& key = msg.get_key();
            block_id_type id(reinterpret_cast<const char*>(key.get_data()), key.get_size());
            auto num = block_header::num_from_id(id);
            elog("delivery_report_callback: block id ${id}, block num ${num}, err ${err}", ("id", id)("num", num)("err", err.to_string()));
        }
    });
    */

    producer_ = std::make_unique<Producer>(config_);

    auto conf = producer_->get_configuration().get_all();
    ilog("Kafka config: ${conf}", ("conf", conf));
}

void kafka::stop() {
    producer_->flush();

    producer_.reset();
}

chainbase::database& get_db() {
    auto plugin = app().find_plugin<chain_plugin>();
    auto& chain = plugin->chain();
    return const_cast<chainbase::database&>(chain.db()); // Override read-only access to state DB (highly unrecommended practice!)
}

void kafka::push_block(const chain::block_state_ptr& block_state, bool irreversible, bool produce) {
    ++poll_counter_;
    if (poll_counter_ >= poll_interval_) { // trigger error callback or delivery report callback
        poll_counter_ = 0; // reset counter
        try {
            // producer_->poll();
            producer_->flush();
        } catch (const std::exception& e) {
            elog("flush failed: block id ${id}, block num ${num}", ("id", block_state->id)("num", block_state->block_num));
            throw;
        }
    }

    auto id = checksum_bytes(block_state->id);

    auto& db = get_db();

    if (irreversible and block_state->block_num > 1) { // block 1 only occurred as irreversible block
        auto bc = db.get<block_cache_object, by_block_id>(block_state->id);
        Block b = fc::json::from_string(string(bc.block.cbegin(), bc.block.cend())).as<Block>();

        // remove all previous cached blocks
        auto& idx = db.get_mutable_index<block_cache_index>();
        const auto& index = idx.indices().get<by_block_num>();
        while ((not index.empty()) and index.begin()->block_num() <= block_state->block_num) {
            idx.remove(*index.begin());
        }

        b.lib = true;
        auto payload = fc::json::to_string(b, fc::json::legacy_generator);
        Buffer buffer(b.id.data(), b.id.size());
        if (produce) {
            producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));
        }
        return;
    }

    const auto& header = block_state->header;
    auto b = std::make_shared<Block>();

    {
        ++producer_stats_counter_; // increase counter

        if (not producer_schedule_) { // initial set producer schedule
            producer_schedule_ = std::make_unique<ProducerSchedule>();
            producer_schedule_->version = block_state->active_schedule.version;
            for (const auto& p: block_state->active_schedule.producers) {
                producer_schedule_->producers.push_back(p.producer_name.value);
            }

            b->schedule = *producer_schedule_;

        } else if (block_state->active_schedule.version > producer_schedule_->version or // must stats when producer schedule changed
                    producer_stats_counter_ >= producer_schedule_->producers.size() * config::producer_repetitions) { // trigger stats every producing loop
            producer_stats_counter_ = 0; // reset counter

            for (const auto& p: producer_schedule_->producers) {
                auto ps = db.get<producer_stats_object, by_producer>(name(p));
                b->producer_stats.push_back(ProducerStats{
                    .producer = ps.producer,
                    .produced_blocks = ps.produced_blocks,
                    .unpaid_blocks = ps.unpaid_blocks
                });
            }

            if (block_state->active_schedule.version > producer_schedule_->version) { // update producer schedule
                producer_schedule_->version = block_state->active_schedule.version;
                producer_schedule_->producers.clear(); // clear old producers
                for (const auto &p: block_state->active_schedule.producers) {
                    producer_schedule_->producers.push_back(p.producer_name.value);
                }

                b->schedule = *producer_schedule_;
            }
        }

        auto p = db.find<producer_stats_object, by_producer>(header.producer);
        if (not p) {
            db.create<producer_stats_object>([&](auto &p) {
                p.producer = header.producer;
                p.produced_blocks = 1;
                p.unpaid_blocks = 1;
                p.claimed_rewards = asset();
            });
        } else {
            db.modify(*p, [&](producer_stats_object &p) {
               p.produced_blocks += 1;
               p.unpaid_blocks += 1;
            });
        }
    }

    b->id = id;
    b->num = block_state->block_num;
    b->timestamp = header.timestamp;

    b->block = fc::raw::pack(*block_state->block);
    b->tx_count = static_cast<uint32_t>(block_state->block->transactions.size());

    uint16_t seq{};
    for (const auto& tx_receipt: block_state->block->transactions) {
        auto count = push_transaction(tx_receipt, b, seq++);

        b->action_count += count.first;
        b->context_free_action_count += count.second;
    }

    cached_traces_.clear();
    cached_actions_.clear();

    {
        const auto& s = db.get<stats_object>();
        db.modify(s, [&](stats_object &s) {
           s.tx_count += b->tx_count;
           s.action_count += b->action_count;
           s.context_free_action_count += b->context_free_action_count;
           if (b->tx_count > s.max_tx_count_per_block) {
               s.max_tx_count_per_block = b->tx_count;
           }
           if (b->action_count > s.max_action_count_per_block) {
               s.max_action_count_per_block = b->action_count;
           }
           if (b->context_free_action_count > s.max_context_free_action_count_per_block) {
               s.max_context_free_action_count_per_block = b->context_free_action_count;
           }

           b->stats.tx_count = s.tx_count;
           b->stats.action_count = s.action_count;
           b->stats.context_free_action_count = s.context_free_action_count;
           b->stats.max_tx_count_per_block = s.max_tx_count_per_block;
           b->stats.max_action_count_per_block = s.max_action_count_per_block;
           b->stats.max_context_free_action_count_per_block = s.max_context_free_action_count_per_block;
           b->stats.account_count = s.account_count;
           b->stats.token_count = s.token_count;
        });
    }

    auto payload = fc::json::to_string(*b, fc::json::legacy_generator);
    Buffer buffer (b->id.data(), b->id.size());
    if (produce) {
        producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));
    }

    if (block_state->block_num > 1) { // block 1 only occurred as irreversible block
        auto bc = db.find<block_cache_object, by_block_id>(block_state->id);
        if (not bc) {
            db.create<block_cache_object>([&](auto &bc) {
               bc.block_id = block_state->id;
               bc.block.assign(payload.cbegin(), payload.cend());
            });
        } else {
            db.modify(*bc, [&](block_cache_object &bc) {
               bc.block.assign(payload.cbegin(), payload.cend());
            });
        }
    }
}

std::pair<uint32_t, uint32_t> kafka::push_transaction(const chain::transaction_receipt& tx_receipt, const BlockPtr& block, uint16_t block_seq) {
    transaction_id_type id;
    auto t = std::make_shared<Transaction>();
    if(tx_receipt.trx.contains<transaction_id_type>()) {
        id = tx_receipt.trx.get<transaction_id_type>();
    } else {
        id = tx_receipt.trx.get<chain::packed_transaction>().id();
    }

    t->id = checksum_bytes(id);
    t->block_id = block->id;
    t->block_num = block->num;
    t->block_time = block->timestamp;
    t->block_seq = block_seq;

    t->status = transactionStatus(tx_receipt.status);
    t->cpu_usage_us = tx_receipt.cpu_usage_us;
    t->net_usage_words = tx_receipt.net_usage_words;

    auto it = cached_traces_.find(id);
    EOS_ASSERT(it != cached_traces_.end() && it->second->receipt, chain::plugin_exception,
               "missing trace for transaction ${id}", ("id", id));
    auto tx_trace = it->second;

    if (tx_trace->except) {
        t->exception = tx_trace->except->to_string();
    }

    block->transactions.push_back(*t);

    auto actions_it = cached_actions_.find(id);
    if (actions_it != cached_actions_.end()) {
        for (auto& a: actions_it->second) {
            block->actions.push_back(*a);
        }
    }

    // only count actions of `executed` transaction
    if (tx_receipt.status == chain::transaction_receipt::executed) {
        for (auto &action_trace: tx_trace->action_traces) {
            if (not action_trace.context_free) t->action_count += 1;
            else t->context_free_action_count += 1;
        }
    }

    return {t->action_count, t->context_free_action_count};
}

void kafka::push_transaction_trace(const chain::transaction_trace_ptr& tx_trace) {
    // bypass failed transaction
    if (not tx_trace->receipt) return;

    // bypass `onblock` transaction
    if (tx_trace->action_traces.size() == 1) {
        const auto& first = tx_trace->action_traces.front().act;
        if (first.account == chain::config::system_account_name and first.name == N(onblock)) {
            return;
        }
    }

    if (tx_trace->failed_dtrx_trace)
        cached_traces_[tx_trace->failed_dtrx_trace->id] = tx_trace;
    else {
        cached_traces_[tx_trace->id] = tx_trace;
    }

    for (auto& action_trace: tx_trace->action_traces) {
        push_action(action_trace, 0); // 0 means no parent
    }
}

asset get_ram_price();
voter_info get_voter(const name &voter);
vector<producer_info> get_producers(const vector<name>& producers);
vector<voter_bonus> get_voter_bonuses(const vector<name>& producers);
vector<voter_bonus> get_voter_bonuses(const name& voter);
void get_voters(const name& from, vector<voter>& voters);

void kafka::push_action(const chain::action_trace& action_trace, uint64_t parent_seq) {
    auto a = std::make_shared<Action>();

    a->global_seq = action_trace.receipt.global_sequence;
    a->recv_seq = action_trace.receipt.recv_sequence;
    a->parent_seq = parent_seq;
    a->account = action_trace.act.account;
    a->name = action_trace.act.name;
    if (not action_trace.act.authorization.empty()) a->auth = fc::raw::pack(action_trace.act.authorization);
    a->data = action_trace.act.data;
    a->receiver = action_trace.receipt.receiver;
    if (not action_trace.receipt.auth_sequence.empty()) a->auth_seq = fc::raw::pack(action_trace.receipt.auth_sequence);
    a->code_seq = action_trace.receipt.code_sequence;
    a->abi_seq = action_trace.receipt.abi_sequence;
    a->block_num = action_trace.block_num;
    a->block_time = action_trace.block_time;
    a->tx_id = checksum_bytes(action_trace.trx_id);
    if (not action_trace.console.empty()) a->console = action_trace.console;

    bool has_ram_deal = false;
    bool has_claimed_rewards = false;
    bool has_claimed_bonus = false;

    try {
        // get any extra data
        if (a->account == a->receiver) { // only once
            const auto& data = action_trace.act.data;

            if (a->account == N(eosio)) {
                switch (a->name) {
                    case N(newaccount): {
                        auto& db = get_db();
                        const auto& s = db.get<stats_object>();
                        db.modify(s, [&](stats_object &s) {
                            s.account_count += 1;
                        });
                        break;
                    }
                    case N(setabi): {
                        const auto setabi = action_trace.act.data_as<chain::setabi>();
                        auto& chain = app().find_plugin<chain_plugin>()->chain();
                        const auto &account_sequence = chain.db().get<chain::account_sequence_object, chain::by_name>(setabi.account);
                        a->extra = fc::json::to_string(account_sequence.abi_sequence, fc::json::legacy_generator);
                        break;
                    }
                    case N(setcode): {
                        const auto setcode = action_trace.act.data_as<chain::setcode>();
                        auto& chain = app().find_plugin<chain_plugin>()->chain();
                        const auto &account_sequence = chain.db().get<chain::account_sequence_object, chain::by_name>(setcode.account);
                        a->extra = fc::json::to_string(account_sequence.code_sequence, fc::json::legacy_generator);
                        break;
                    }
                    case N(canceldelay): {
                        const auto canceldelay = action_trace.act.data_as<chain::canceldelay>();
                        a->extra = fc::json::to_string(canceldelay.trx_id, fc::json::legacy_generator);
                        break;
                    }
                    case N(buyrambytes): {
                        const auto brb = fc::raw::unpack<buyrambytes>(data);
                        cached_ram_deals_[a->global_seq] = ram_deal{
                            .global_seq = a->global_seq,
                            .bytes = brb.bytes,
                            .quantity = asset()
                        };
                        has_ram_deal = true;
                        break;
                    }
                    case N(buyram): {
                        const auto br = fc::raw::unpack<buyram>(data);
                        auto ram_price = get_ram_price();
                        auto r = ram_deal{
                           .global_seq = a->global_seq,
                           .bytes = static_cast<int64_t>(static_cast<double>(br.tokens.get_amount()) / ram_price.get_amount() * 1024), // estimation
                           .quantity = br.tokens
                        };
                        a->extra = fc::json::to_string(r, fc::json::legacy_generator);
                        break;
                    }
                    case N(sellram): {
                        const auto sr = fc::raw::unpack<sellram>(data);
                        cached_ram_deals_[a->global_seq] = ram_deal{
                           .global_seq = a->global_seq,
                           .bytes = -sr.bytes,
                           .quantity = asset()
                        };
                        has_ram_deal = true;
                        break;
                    }
                    case N(delegatebw): {
                        const auto db = fc::raw::unpack<delegatebw>(data);
                        const auto& from = db.transfer ? db.receiver : db.from;
                        vector<voter> result;
                        get_voters(from, result);
                        a->extra = fc::json::to_string(result, fc::json::legacy_generator);
                        // ilog("delegatebw: ${extra}", ("extra", a->extra));
                        break;
                    }
                    case N(undelegatebw): {
                        const auto ub = fc::raw::unpack<undelegatebw>(data);
                        vector<voter> result;
                        get_voters(ub.from, result);
                        a->extra = fc::json::to_string(result, fc::json::legacy_generator);
                        // ilog("undelegatebw: ${extra}", ("extra", a->extra));
                        break;
                    }
                    case N(voteproducer): {
                        const auto vp = fc::raw::unpack<voteproducer>(data);
                        vector<voter> result;
                        get_voters(vp.voter, result);
                        a->extra = fc::json::to_string(result, fc::json::legacy_generator);
                        // ilog("voteproducer: ${extra}", ("extra", a->extra));
                        break;
                    }
                    case N(regproxy): {
                        const auto rp = fc::raw::unpack<regproxy>(data);
                        vector<voter> result;
                        get_voters(rp.proxy, result);
                        a->extra = fc::json::to_string(result, fc::json::legacy_generator);
                        // ilog("regproxy: ${extra}", ("extra", a->extra));
                        break;
                    }
                    case N(regproducer): {
                        const auto rp = fc::raw::unpack<regproducer>(data);

                        auto& db = get_db();
                        auto p = db.find<producer_stats_object, by_producer>(rp.producer);
                        if (not p) {
                            db.create<producer_stats_object>([&](auto &p) {
                               p.producer = rp.producer;
                               p.produced_blocks = 0;
                               p.unpaid_blocks = 0;
                               p.claimed_rewards = asset();
                            });
                        }

                        break;
                    }
                    case N(unregprod): {
                        // const auto up = fc::raw::unpack<unregprod>(data);
                        break;
                    }
                    case N(rmvproducer): {
                        // const auto rp = fc::raw::unpack<rmvproducer>(data);
                        break;
                    }
                    case N(claimrewards): {
                        const auto cr = fc::raw::unpack<claimrewards>(data);
                        cached_claimed_rewards_[a->global_seq] = claimed_rewards{
                            .owner = cr.owner,
                            .quantity = asset(),
                            .voter_bonus_balance = asset()
                        };
                        has_claimed_rewards = true;
                        break;
                    }
                    case N(claimbonus): {
                        const auto cb = fc::raw::unpack<claimbonus>(data);
                        cached_claimed_bonus_[a->global_seq] = claimed_bonus{
                            .owner = cb.owner,
                            .quantity = asset(),
                            .balances = {}
                        };
                        has_claimed_bonus = true;
                        break;
                    }
                }
            } else if (a->name == N(create) and is_token(a->account)) {
                try {
                    const auto create_data = fc::raw::unpack<create>(data);
                    a->extra = fc::json::to_string(create_data, fc::json::legacy_generator);

                    auto& db = get_db();
                    const auto& s = db.get<stats_object>();
                    db.modify(s, [&](stats_object &s) {
                       s.token_count += 1;
                    });
                } catch (...) {
                    // ignore any error of unpack
                }
            } else if (a->name == N(issue) and is_token(a->account)) {
                try {
                    const auto issue_data = fc::raw::unpack<issue>(data);
                    a->extra = fc::json::to_string(issue_data, fc::json::legacy_generator);
                } catch (...) {
                    // ignore any error of unpack
                }
            } else if (a->name == N(transfer) and is_token(a->account)) {
                std::unique_ptr<transfer> transfer_ptr;
                try {
                    transfer_ptr = std::make_unique<transfer>(fc::raw::unpack<transfer>(data));
                } catch (...) {
                    // ignore any error of unpack
                }
                if (transfer_ptr) {
                    // TODO: get table row
                    a->extra = fc::json::to_string(transfer_ptr, fc::json::legacy_generator);

                    if (a->account == N(eosio.token)) {
                        if (transfer_ptr->from == N(eosio.ram) or transfer_ptr->from == N(eosio.ramfee)) { // buy
                           auto it = cached_ram_deals_.find(a->parent_seq);
                           if (it != cached_ram_deals_.end()) {
                               if (it->second.quantity.get_amount() == 0) it->second.quantity = transfer_ptr->quantity;
                               else it->second.quantity += transfer_ptr->quantity;
                           }
                        } else if (transfer_ptr->to == N(eosio.ram)) { // sell
                            auto it = cached_ram_deals_.find(a->parent_seq);
                            if (it != cached_ram_deals_.end()) {
                                if (it->second.quantity.get_amount() == 0) it->second.quantity = transfer_ptr->quantity;
                                else it->second.quantity += transfer_ptr->quantity;
                            }
                        } else if (transfer_ptr->to == N(eosio.ramfee)) { // sell
                            auto it = cached_ram_deals_.find(a->parent_seq);
                            if (it != cached_ram_deals_.end()) {
                                if (it->second.quantity.get_amount() == 0) it->second.quantity = -transfer_ptr->quantity;
                                else it->second.quantity -= transfer_ptr->quantity;
                            }
                        } else if (transfer_ptr->from == N(eosio.bpay) or transfer_ptr->from == N(eosio.vpay)) { // producer block/vote pay
                            {
                                auto it = cached_claimed_rewards_.find(a->parent_seq);
                                if (it != cached_claimed_rewards_.end() and it->second.owner == transfer_ptr->to) {
                                    if (it->second.quantity.get_amount() == 0) it->second.quantity = transfer_ptr->quantity;
                                    else it->second.quantity += transfer_ptr->quantity;
                                }
                            }
                            {
                                auto it = cached_claimed_bonus_.find(a->parent_seq);
                                if (it != cached_claimed_bonus_.end() and it->second.owner == transfer_ptr->to) {
                                    if (it->second.quantity.get_amount() == 0) it->second.quantity = transfer_ptr->quantity;
                                    else it->second.quantity += transfer_ptr->quantity;
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (fc::exception& e) {
        elog("filter action data failed, error: ${e}, action trace: ${a}", ("e", e.to_string())("a", action_trace));
        throw;
    }

    cached_actions_[action_trace.trx_id].push_back(a);

    for (auto& inline_trace: action_trace.inline_traces) {
        push_action(inline_trace, action_trace.receipt.global_sequence);
    }

    if (has_ram_deal) {
        auto it = cached_ram_deals_.find(a->global_seq);
        if (it != cached_ram_deals_.end()) {
            a->extra = fc::json::to_string(it->second, fc::json::legacy_generator);
        }
    }

    if (has_claimed_rewards) {
        auto it = cached_claimed_rewards_.find(a->global_seq);
        if (it != cached_claimed_rewards_.end()) {
            auto& db = get_db();

            auto p = db.find<producer_stats_object, by_producer>(it->second.owner);
            if (not p) {
                db.create<producer_stats_object>([&](auto &ps) {
                    ps.producer = it->second.owner;
                    ps.produced_blocks = 0;
                    ps.unpaid_blocks = 0;
                    ps.claimed_rewards = it->second.quantity;
                });
            } else {
                if (it->second.quantity.get_amount() == 0) it->second.quantity = p->claimed_rewards;
                else if (p->claimed_rewards.get_amount() != 0) it->second.quantity += p->claimed_rewards; // add
                db.modify(*p, [&](producer_stats_object &ps) {
                    ps.claimed_rewards = it->second.quantity;
                    ps.unpaid_blocks = 0; // reset when claim rewards
                });
            }

            auto vb = get_voter_bonuses(vector<name>{it->second.owner});
            if (not vb.empty()) {
                it->second.voter_bonus_balance = vb.front().balance;
            }

            a->extra = fc::json::to_string(it->second, fc::json::legacy_generator);
        }
    }

    if (has_claimed_bonus) {
        auto it = cached_claimed_bonus_.find(a->global_seq);
        if (it != cached_claimed_bonus_.end()) {
            auto& db = get_db();

            auto v = db.find<voter_stats_object, by_voter>(it->second.owner);
            if (not v) {
                db.create<voter_stats_object>([&](auto &vs) {
                    vs.voter = it->second.owner;
                    vs.claimed_bonus = it->second.quantity;
                });
            } else {
                if (it->second.quantity.get_amount() == 0) it->second.quantity = v->claimed_bonus;
                else if (v->claimed_bonus.get_amount() != 0) it->second.quantity += v->claimed_bonus; // add
                db.modify(*v, [&](voter_stats_object &vs) {
                    vs.claimed_bonus = it->second.quantity;
                });
            }

            auto vb = get_voter_bonuses(it->second.owner);
            it->second.balances = fc::move(vb);

            a->extra = fc::json::to_string(it->second, fc::json::legacy_generator);
        }
    }

    if (parent_seq == 0) {
        if (not cached_ram_deals_.empty()) cached_ram_deals_.clear();
        if (not cached_claimed_rewards_.empty()) cached_claimed_rewards_.clear();
        if (not cached_claimed_bonus_.empty()) cached_claimed_bonus_.clear();
    }
}

bool abi_field_def_equal(const vector<chain::field_def>& a, vector<chain::field_def>& b) {
    for (auto& f: b) {
        if (f.type == "account_name") f.type = "name"; // adapt to type alias
    }
    return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend());
}

bool kafka::is_token(name account) {
    if (cached_tokens_.count(account)) return true;

    auto plugin = app().find_plugin<chain_plugin>();
    auto& chain = plugin->chain();
    auto abi_serializer = chain.get_abi_serializer(account, plugin->get_abi_serializer_max_time());
    auto account_object = chain.get_account(account);
    if (not abi_serializer) return false; // account with no abi can also be called as contract

    auto stat_name = abi_serializer->get_table_type(N(stat));
    if (stat_name.empty()) return false;
    auto stat_struct = abi_serializer->get_struct(stat_name);
    static const vector<chain::field_def> stat_fields{
        {"supply", "asset"}, {"max_supply", "asset"}, {"issuer", "name"}
    };
    if (not abi_field_def_equal(stat_fields, stat_struct.fields)) {
        return false;
    }

    auto accounts_name = abi_serializer->get_table_type(N(accounts));
    if (accounts_name.empty()) return false;
    auto accounts_struct = abi_serializer->get_struct(accounts_name);
    static const vector<chain::field_def> accounts_fields{
        {"balance", "asset"}
    };
    if (not abi_field_def_equal(accounts_fields, accounts_struct.fields)) {
        return false;
    }

    auto create_name = abi_serializer->get_action_type(N(create));
    if (create_name.empty()) return false;
    auto create_struct = abi_serializer->get_struct(create_name);
    static const vector<chain::field_def> create_fields{
        {"issuer", "name"}, {"maximum_supply", "asset"}
    };
    if (not abi_field_def_equal(create_fields, create_struct.fields)) {
        return false;
    }

    auto issue_name = abi_serializer->get_action_type(N(issue));
    if (issue_name.empty()) return false;
    auto issue_struct = abi_serializer->get_struct(issue_name);
    static const vector<chain::field_def> issue_fields{
        {"to", "name"}, {"quantity", "asset"}, {"memo", "string"}
    };
    if (not abi_field_def_equal(issue_fields, issue_struct.fields)) {
        return false;
    }

    auto transfer_name = abi_serializer->get_action_type(N(transfer));
    if (transfer_name.empty()) return false;
    auto transfer_struct = abi_serializer->get_struct(transfer_name);
    static const vector<chain::field_def> transfer_fields{
        {"from", "name"}, {"to", "name"}, {"quantity", "asset"}, {"memo", "string"}
    };
    if (not abi_field_def_equal(transfer_fields, transfer_struct.fields)) {
        return false;
    }

    if (cached_tokens_.size() > 1000000) cached_tokens_.clear(); // avoid memory overflow

    cached_tokens_.insert(account);

    return true;
}

asset get_ram_price() {
    auto& chain = app().get_plugin<chain_plugin>();
    auto ro_api = chain.get_read_only_api();

    chain_apis::read_only::get_table_rows_params p;
    p.json = true;
    p.code = N(eosio);
    p.scope = "eosio";
    p.table = "rammarket";
    auto rammarket = ro_api.get_table_rows(p);
    EOS_ASSERT(not rammarket.rows.empty(), chain::contract_exception, "missing rammarket");
    auto& row = rammarket.rows.front();

    // auto supply = row["supply"].as<asset>();
    auto& base = row["base"].get_object();
    auto& quote = row["quote"].get_object();
    auto base_balance = base["balance"].as<asset>();
    auto quote_balance = quote["balance"].as<asset>();
    // auto base_weight = base["weight"].as_double();
    // auto quote_weight = quote["weight"].as_double();

    auto precision = quote_balance.precision();
    // tokens per KB ram
    auto price = static_cast<double>(quote_balance.get_amount() *  precision) / (base_balance.get_amount() + 1) * 1024 / precision;

    /*
    exchange_state state;
    state.supply = fc::move(supply);
    state.base = exchange_state::connector{fc::move(base_balance), base_weight};
    state.quote = exchange_state::connector{fc::move(quote_balance), quote_weight};
    */

    return asset(static_cast<int64_t>(price), quote_balance.get_symbol());
}

voter_info get_voter(const name &voter) {
    auto& chain = app().get_plugin<chain_plugin>();
    auto ro_api = chain.get_read_only_api();

    chain_apis::read_only::get_table_rows_params p;
    p.json = false;
    p.code = N(eosio);
    p.scope = "eosio";
    p.table = "voters";
    p.limit = 1;
    p.lower_bound = voter.to_string();
    auto voters = ro_api.get_table_rows(p);
    EOS_ASSERT(not voters.rows.empty(), chain::contract_exception, "missing voters");
    auto& row = voters.rows.front();

    return fc::raw::unpack<voter_info>(row.as<vector<char>>());
}

vector<producer_info> get_producers(const vector<name>& producers) {
    auto& chain = app().get_plugin<chain_plugin>();
    auto ro_api = chain.get_read_only_api();

    vector<string> ps;
    ps.reserve(producers.size());
    for (auto& p: producers) {
        ps.push_back(p.to_string());
    }

    auto r = ro_api.get_producers_by_names(chain_apis::read_only::get_producers_by_names_params{.producers = ps});

    vector<producer_info> result;
    result.reserve(r.size());
    for (auto& p: r) {
        result.push_back(fc::raw::unpack<producer_info>(p.as<vector<char>>()));
    }
    for (size_t i = 0; i < result.size(); ++i) {
        EOS_ASSERT(result[i].owner == producers[i], chain::contract_exception, "inconsistent producer name");
    }
    return result;
}

vector<voter_bonus> get_voter_bonuses(const vector<name>& producers) {
    auto& chain = app().get_plugin<chain_plugin>();
    auto ro_api = chain.get_read_only_api();

    vector<string> ps;
    ps.reserve(producers.size());
    for (auto& p: producers) {
        ps.push_back(p.to_string());
    }

    auto r = ro_api.get_voter_bonuses_by_names(chain_apis::read_only::get_voter_bonuses_by_names_params{.producers = ps});

    vector<voter_bonus> result;
    result.reserve(r.size());
    for (auto& p: r) {
       if (not p.is_null()) result.push_back(fc::raw::unpack<voter_bonus>(p.as<vector<char>>()));
    }
    return result;
}

vector<voter_bonus> get_voter_bonuses(const name& voter) {
    const auto v = get_voter(voter);

    vector<name> producers;
    if (v.proxy) {
        auto p = get_voter(v.proxy);
        producers = p.producers;
    } else {
        producers = v.producers;
    }

    return get_voter_bonuses(producers);
}

void get_voters(const name& from, vector<voter>& voters) {
    auto v = get_voter(from);

    voter result{
       .owner = v.owner, .proxy = v.proxy, .staked = v.staked, .last_vote_weight = v.last_vote_weight,
       .proxied_vote_weight = v.proxied_vote_weight, .is_proxy = v.is_proxy, .last_change_time = v.last_change_time
    };
    if (not v.producers.empty()) {
        auto producers = get_producers(v.producers);
        result.producers.reserve(producers.size());
        for (auto& p: producers) {
            result.producers.push_back(producer{.owner = p.owner, .total_votes = p.total_votes});
        }
    }

    voters.push_back(fc::move(result));

    if (v.proxy) {
        get_voters(v.proxy, voters);
    }
}

}

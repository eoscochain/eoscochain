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

void kafka::start() {
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

void kafka::push_block(const chain::block_state_ptr& block_state, bool irreversible) {
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
        Buffer buffer (b.id.data(), b.id.size());
        producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));
        return;
    }

    const auto& header = block_state->header;
    auto b = std::make_shared<Block>();

    {
        ++producer_stats_interval_; // increase counter

        if (not producer_schedule_) { // initial set producer schedule
            producer_schedule_ = std::make_unique<producer_schedule>();
            producer_schedule_->version = block_state->active_schedule.version;
            for (const auto& p: block_state->active_schedule.producers) {
                producer_schedule_->producers.push_back(p.producer_name);
            }
        } else if (block_state->active_schedule.version > producer_schedule_->version or // must stats when producer schedule changed
                    producer_stats_interval_ >= producer_schedule_->producers.size() * config::producer_repetitions) { // trigger stats every producing loop
            producer_stats_interval_ = 0; // reset counter

            for (const auto& p: producer_schedule_->producers) {
                auto ps = db.get<producer_stats_object, by_producer>(p);
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
                    producer_schedule_->producers.push_back(p.producer_name);
                }
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
    producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));

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
                        .quantity = asset()
                    };
                    has_claimed_rewards = true;
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
            try {
                const auto transfer_data = fc::raw::unpack<transfer>(data);
                // TODO: get table row
                a->extra = fc::json::to_string(transfer_data, fc::json::legacy_generator);

                if (a->account == N(eosio.token)) {
                    if (transfer_data.from == N(eosio.ram) or transfer_data.from == N(eosio.ramfee)) { // buy
                       auto it = cached_ram_deals_.find(a->parent_seq);
                       if (it != cached_ram_deals_.end()) it->second.quantity += transfer_data.quantity;
                    } else if (transfer_data.to == N(eosio.ram)) { // sell
                        auto it = cached_ram_deals_.find(a->parent_seq);
                        if (it != cached_ram_deals_.end()) it->second.quantity += transfer_data.quantity;
                    } else if (transfer_data.to == N(eosio.ramfee)) { // sell
                        auto it = cached_ram_deals_.find(a->parent_seq);
                        if (it != cached_ram_deals_.end()) it->second.quantity -= transfer_data.quantity;
                    } else if (transfer_data.from == N(eosio.bpay) or transfer_data.from == N(eosio.vpay)) { // producer block/vote pay
                        auto it = cached_claimed_rewards_.find(a->parent_seq);
                        if (it != cached_claimed_rewards_.end() and it->second.owner == transfer_data.to) {
                            it->second.quantity += transfer_data.quantity;
                        }
                    }
                }
            } catch (...) {
                // ignore any error of unpack
            }
        }
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
            auto plugin = app().find_plugin<chain_plugin>();
            auto& chain = plugin->chain();
            chainbase::database& db = const_cast<chainbase::database&>(chain.db()); // Override read-only access to state DB (highly unrecommended practice!)

            auto p = db.find<producer_stats_object, by_producer>(it->second.owner);
            if (not p) {
                db.create<producer_stats_object>([&](auto &ps) {
                    ps.producer = it->second.owner;
                    ps.produced_blocks = 0;
                    ps.unpaid_blocks = 0;
                    ps.claimed_rewards = it->second.quantity;
                });
            } else {
                it->second.quantity += p->claimed_rewards; // add
                db.modify(*p, [&](producer_stats_object &ps) {
                    ps.claimed_rewards = it->second.quantity;
                    ps.unpaid_blocks = 0; // reset when claim rewards
                });
            }

            a->extra = fc::json::to_string(it->second, fc::json::legacy_generator);
        }
    }

    if (parent_seq == 0) {
        if (not cached_ram_deals_.empty()) cached_ram_deals_.clear();
        if (not cached_claimed_rewards_.empty()) cached_claimed_rewards_.clear();
    }
}

bool kafka::is_token(name account) {
    if (cached_tokens_.count(account)) return true;

    auto plugin = app().find_plugin<chain_plugin>();
    auto& chain = plugin->chain();
    auto abi_serializer = chain.get_abi_serializer(account, plugin->get_abi_serializer_max_time());
    EOS_ASSERT(bool(abi_serializer), chain::abi_exception, "invalid abi for account ${account}", ("account", account));

    auto stat_name = abi_serializer->get_table_type(N(stat));
    if (stat_name.empty()) return false;
    auto stat_struct = abi_serializer->get_struct(stat_name);
    static const vector<chain::field_def> stat_fields{
        {"supply", "asset"}, {"max_supply", "asset"}, {"issuer", "account_name"}
    };
    if (not std::equal(stat_struct.fields.cbegin(),
                       stat_struct.fields.cend(),
                       stat_fields.cbegin(),
                       stat_fields.cend())) {
        return false;
    }

    auto accounts_name = abi_serializer->get_table_type(N(accounts));
    if (accounts_name.empty()) return false;
    auto accounts_struct = abi_serializer->get_struct(accounts_name);
    static const vector<chain::field_def> accounts_fields{
        {"balance", "asset"}
    };
    if (not std::equal(accounts_struct.fields.cbegin(),
                       accounts_struct.fields.cend(),
                       accounts_fields.cbegin(),
                       accounts_fields.cend())) {
        return false;
    }

    auto create_name = abi_serializer->get_action_type(N(create));
    if (create_name.empty()) return false;
    auto create_struct = abi_serializer->get_struct(create_name);
    static const vector<chain::field_def> create_fields{
        {"issuer", "account_name"}, {"maximum_supply", "asset"}
    };
    if (not std::equal(create_struct.fields.cbegin(),
                       create_struct.fields.cend(),
                       create_fields.cbegin(),
                       create_fields.cend())) {
        return false;
    }

    auto issue_name = abi_serializer->get_action_type(N(issue));
    if (issue_name.empty()) return false;
    auto issue_struct = abi_serializer->get_struct(issue_name);
    static const vector<chain::field_def> issue_fields{
        {"to", "account_name"}, {"quantity", "asset"}, {"memo", "string"}
    };
    if (not std::equal(issue_struct.fields.cbegin(),
                       issue_struct.fields.cend(),
                       issue_fields.cbegin(),
                       issue_fields.cend())) {
        return false;
    }

    auto transfer_name = abi_serializer->get_action_type(N(transfer));
    if (transfer_name.empty()) return false;
    auto transfer_struct = abi_serializer->get_struct(transfer_name);
    static const vector<chain::field_def> transfer_fields{
        {"from", "account_name"}, {"to", "account_name"}, {"quantity", "asset"}, {"memo", "string"}
    };
    if (not std::equal(transfer_struct.fields.cbegin(),
                       transfer_struct.fields.cend(),
                       transfer_fields.cbegin(),
                       transfer_fields.cend())) {
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

void get_voters(const name& from, vector<voter>& voters) {
    auto v = get_voter(from);

    voter result{
       .owner = v.owner, .proxy = v.proxy, .staked = v.staked, .last_vote_weight = v.last_vote_weight,
       .proxied_vote_weight = v.proxied_vote_weight, .is_proxy = v.is_proxy
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

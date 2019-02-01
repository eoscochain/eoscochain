#include "kafka.hpp"

#include <eosio/chain/config.hpp>
#include <fc/io/json.hpp>

#include "try_handle.hpp"
#include "actions.hpp"

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

void kafka::push_block(const chain::block_state_ptr& block_state, bool irreversible) {
    auto id = checksum_bytes(block_state->id);

    if (irreversible) {
        IrreversibleBlock b{id, block_state->block_num};
        auto payload = fc::json::to_string(b, fc::json::legacy_generator);
        auto num_str = std::to_string(b.num);
        Buffer buffer (num_str.data(), num_str.size());
        producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));
        return;
    }

    const auto& header = block_state->header;
    auto b = std::make_shared<Block>();

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

    auto payload = fc::json::to_string(*b, fc::json::legacy_generator);
    Buffer buffer (b->id.data(), b->id.size());
    producer_->produce(MessageBuilder(topic_).partition(partition_).key(buffer).payload(payload));
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

    // get any extra data
    if (a->account == a->receiver) { // only once
        const auto& data = action_trace.act.data;

        if (a->account == N(eosio)) {
            switch (a->name) {
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
                    break;
                }
                case N(buyram): {
                    const auto br = fc::raw::unpack<buyram>(data);
                    auto ram_price = get_ram_price();
                    cached_ram_deals_[a->global_seq] = ram_deal{
                       .global_seq = a->global_seq,
                       .bytes = br.tokens.get_amount(),
                       .quantity = br.tokens
                    };
                    break;
                }
                case N(sellram): {
                    const auto sr = fc::raw::unpack<sellram>(data);
                    cached_ram_deals_[a->global_seq] = ram_deal{
                       .global_seq = a->global_seq,
                       .bytes = -sr.bytes,
                       .quantity = asset()
                    };
                    break;
                }
            }
        } else if (a->name == N(create) and is_token(a->account)) {
            try {
                const auto create_data = fc::raw::unpack<create>(data);
                a->extra = fc::json::to_string(create_data, fc::json::legacy_generator);
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

                if (a->account == N(eosio)) {
                    if (transfer_data.from == N(eosio.ram)) {
                    } else if (transfer_data.to == N(eosio.ram)) {
                    } else if (transfer_data.from == N(eosio.ramfee)) {
                    } else if (transfer_data.to == N(eosio.ramfee)) {
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

    auto create_name = abi_serializer->get_table_type(N(create));
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

    auto issue_name = abi_serializer->get_table_type(N(issue));
    if (issue_name.empty()) return false;
    auto issue_struct = abi_serializer->get_struct(issue_name);
    static const vector<chain::field_def> issue_fields{
        {"balance", "asset"}, {"to", "account_name"}, {"quantity", "asset"}, {"memo", "string"}
    };
    if (not std::equal(issue_struct.fields.cbegin(),
                       issue_struct.fields.cend(),
                       issue_fields.cbegin(),
                       issue_fields.cend())) {
        return false;
    }

    auto transfer_name = abi_serializer->get_table_type(N(transfer));
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
    auto& base = row["base"].get_object();
    auto& quote = row["quote"].get_object();
    auto base_balance = base["balance"].as<asset>();
    auto quote_balance = quote["balance"].as<asset>();
    auto base_weight = base["weight"].as_double();
    auto quote_weight = quote["weight"].as_double();
    return asset();
}

}

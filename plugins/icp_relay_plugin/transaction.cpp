#include "icp_relay.hpp"

#include <eosio/producer_plugin/producer_plugin.hpp>
#include <fc/io/json.hpp>

#include "api.hpp"

namespace icp {

void print_action( const fc::variant& at ) {
   const auto& receipt = at["receipt"];
   auto receiver = receipt["receiver"].as_string();
   const auto& act = at["act"].get_object();
   auto code = act["account"].as_string();
   auto func = act["name"].as_string();
   auto args = fc::json::to_string( act["data"] );
   auto console = at["console"].as_string();

   if( args.size() > 100 ) args = args.substr(0,100) + "...";
   cout << "#" << std::setw(14) << right << receiver << " <= " << std::setw(28) << std::left << (code +"::" + func) << " " << args << "\n";
   if( console.size() ) {
      std::stringstream ss(console);
      string line;
      std::getline( ss, line );
      cout << ">> " << line << "\n";
   }
}

void print_action_tree( const fc::variant& action ) {
   print_action( action );
   const auto& inline_traces = action["inline_traces"].get_array();
   for( const auto& t : inline_traces ) {
      print_action_tree( t );
   }
}

void print_result(const fc::variant& processed) { try {
   const auto& transaction_id = processed["id"].as_string();
   string status = processed["receipt"].is_object() ? processed["receipt"]["status"].as_string() : "failed";
   int64_t net = -1;
   int64_t cpu = -1;
   if( processed.get_object().contains( "receipt" )) {
      const auto& receipt = processed["receipt"];
      if( receipt.is_object()) {
         net = receipt["net_usage_words"].as_int64() * 8;
         cpu = receipt["cpu_usage_us"].as_int64();
      }
   }

   cerr << status << " transaction: " << transaction_id << "  ";
   if( net < 0 ) {
      cerr << "<unknown>";
   } else {
      cerr << net;
   }
   cerr << " bytes  ";
   if( cpu < 0 ) {
      cerr << "<unknown>";
   } else {
      cerr << cpu;
   }

   cerr << " us\n";

   if( status == "failed" ) {
      auto soft_except = processed["except"].as<optional<fc::exception>>();
      if( soft_except ) {
         edump((soft_except->to_detail_string()));
      }
   } else {
      const auto& actions = processed["action_traces"].get_array();
      for( const auto& a : actions ) {
         print_action_tree( a );
      }
      wlog( "\rwarning: transaction executed locally, but may not be confirmed by the network yet" );
   }
} FC_CAPTURE_AND_RETHROW( (processed) ) }

void relay::push_transaction(vector<action> actions, function<void(bool)> callback, packed_transaction::compression_type compression) {
   auto& chain = app().get_plugin<chain_plugin>();

   signed_transaction trx;
   trx.actions = std::forward<decltype(actions)>(actions);
   vector<action_name> action_names;
   for (auto& a: trx.actions) {
      a.account = local_contract_;
      if (a.authorization.empty()) a.authorization = signer_;
      action_names.push_back(a.name);
   }

   trx.expiration = chain.chain().head_block_time() + tx_expiration_;
   trx.set_reference_block(chain.chain().last_irreversible_block_id());
   trx.max_cpu_usage_ms = tx_max_cpu_usage_;
   trx.max_net_usage_words = (tx_max_net_usage_ + 7)/8;
   trx.delay_sec = delaysec_;

   auto pp = app().find_plugin<producer_plugin>();
   FC_ASSERT(pp and pp->get_state() == abstract_plugin::started, "producer_plugin not found");

   if (signer_required_keys_.empty()) {
      auto available_keys = pp->get_producer_keys();
      auto ro_api = chain.get_read_only_api();
      fc::variant v{static_cast<const transaction&>(trx)};
      signer_required_keys_ = ro_api.get_required_keys(chain_apis::read_only::get_required_keys_params{v, available_keys}).required_keys;
   }

   auto digest = trx.sig_digest(chain.get_chain_id(), trx.context_free_data);
   for (auto& k: signer_required_keys_) {
      trx.signatures.push_back(pp->sign_compact(k, digest));
   }

   auto packet_tx = fc::mutable_variant_object(packed_transaction(trx, compression));
   auto rw_api = chain.get_read_write_api();
   rw_api.push_transaction(fc::variant_object(packet_tx), [action_names, callback](const fc::static_variant<fc::exception_ptr, chain_apis::read_write::push_transaction_results>& result) {
      wlog("actions: ${a}", ("a", action_names));
      if (result.contains<fc::exception_ptr>()) {
         auto& e = result.get<fc::exception_ptr>();
         if (e->code() == 3040008) wlog("${e}", ("e", e->to_detail_string())); // tx_duplicate
         else elog("${e}", ("e", e->to_detail_string()));
         if (callback) callback(false);
      } else {
         auto& r = result.get<chain_apis::read_write::push_transaction_results>();
         // wlog("transaction ${id}: ${processed}", ("id", r.transaction_id)("processed", fc::json::to_string(p)));
         print_result(r.processed);
         if (callback) callback(true);
      }
   });
}

}

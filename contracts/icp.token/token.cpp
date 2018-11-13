#include "icp.token.hpp"

namespace icp {

   string trim(const string& str) {
      size_t first = str.find_first_not_of(' ');
      if (string::npos == first)
      {
         return str;
      }
      size_t last = str.find_last_not_of(' ');
      return str.substr(first, (last - first + 1));
   }

   constexpr uint8_t max_precision = 18;

   symbol_name string_to_symbol(const string& from) {
         auto s = trim(from);
         eosio_assert(!s.empty(), "creating symbol from empty string");
         auto comma_pos = s.find(',');
         eosio_assert(comma_pos != string::npos, "missing comma in symbol");
         auto prec_part = s.substr(0, comma_pos);
         uint8_t p = std::stoull(prec_part);
         string name_part = s.substr(comma_pos + 1);
         eosio_assert( p <= max_precision, "precision should be <= 18");
         return symbol_name(eosio::string_to_symbol(p, name_part.c_str()));
   }

   void token::create(account_name contract, string symbol) {
      require_auth(_self);

      auto sym = symbol_type(string_to_symbol(symbol));
      eosio_assert(sym.is_valid(), "invalid symbol name");

      stats statstable(_self, contract);
      auto existing = statstable.find(sym.name());
      eosio_assert(existing == statstable.end(), "token with symbol already exists");

      statstable.emplace(_self, [&](auto &s) {
         s.supply.symbol = sym;
      });
   }

   void token::transfer(account_name contract, account_name from, account_name to, asset quantity, string memo) {
      eosio_assert(from != to, "cannot transfer to self");
      require_auth(from);
      eosio_assert(is_account(to), "to account does not exist");
      auto sym = quantity.symbol.name();
      stats statstable(_self, contract);
      const auto &st = statstable.get(sym);

      require_recipient(from);
      require_recipient(to);

      eosio_assert(quantity.is_valid(), "invalid quantity");
      eosio_assert(quantity.amount > 0, "must transfer positive quantity");
      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

      sub_balance(contract, from, quantity);
      add_balance(contract, to, quantity, from);
   }

   void token::sub_balance(account_name contract, account_name owner, asset value) {
      accounts from_acnts(_self, contract);

      auto by_account_asset = from_acnts.get_index<N(accountasset)>();
      const auto& from = by_account_asset.get(account_asset_key(owner, value), "no balance object found");
      eosio_assert(from.balance.amount >= value.amount, "overdrawn balance");

      if (from.balance.amount == value.amount) {
         from_acnts.erase(from);
      } else {
         from_acnts.modify(from, owner, [&](auto &a) {
            a.balance -= value;
         });
      }
   }

   void token::add_balance(account_name contract, account_name owner, asset value, account_name ram_payer) {
      accounts to_acnts(_self, contract);
      auto by_account_asset = to_acnts.get_index<N(accountasset)>();
      auto to = by_account_asset.find(account_asset_key(owner, value));
      if (to == by_account_asset.end()) {
         to_acnts.emplace(ram_payer, [&](auto &a) {
            a.pk = to_acnts.available_primary_key();
            a.account = owner;
            a.balance = value;
         });
      } else {
         to_acnts.modify(*to, 0, [&](auto &a) {
            a.balance += value;
         });
      }
   }

}

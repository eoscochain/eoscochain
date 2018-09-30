#include "icp.token.hpp"

#include <eosio.token/eosio.token.hpp>
#include <eosiolib/datastream.hpp>
#include <eosiolib/action.hpp>
#include <icp/icp.hpp>

namespace icp {

   constexpr char EQUIVALENT_SYMBOL_PREFIX = 'C';

   token::token(account_name self) : contract(self) {
      peer_singleton peer(_self, _self);
      icp_singleton icp(_self, _self);
      _peer = peer.get_or_default(peer_contract{})
      _icp = icp.get_or_default(icp_contract{})
   }

   void token::seticp(acccount_name icp) {
      icp_singleton p(_self, _self);
      eosio_assert(!p.exists(), "local icp contract name already exists");

      p.set(icp_contract{icp}, _self);
   }

   void token::setpeer(account_name peer) {
      peer_singleton p(_self, _self);
      eosio_assert(!p.exists(), "remote peer contract name already exists");

      p.set(peer_contract{peer}, _self);
   }

   void token::create(account_name issuer, symbol_name symbol) {
      require_auth(_self);

      auto sym = symbol_type(symbol);
      eosio_assert(sym.is_valid(), "invalid symbol name");

      stats statstable(_self, sym.name());
      auto existing = statstable.find(sym.name());
      eosio_assert(existing == statstable.end(), "token with symbol already exists");

      statstable.emplace(_self, [&](auto &s) {
         s.supply.symbol = sym;
         s.issuer = issuer;
      });
   }

   void token::icpto(account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration) {
      eosio_assert(_peer.peer, "empty remote peer contract");
      eosio_assert(_icp.icp, "empty local icp contract");

      auto seq = eosio::icp(_icp.icp).next_packet_seq();

      auto icp_send = action(vector<permission_level>{}, _peer.peer, N(icpfrom), eosio::token::transfer_args{from, icp_to, quantity, memo});
      auto icp_receive = action(vector<permission_level>{}, _self, N(icpreceipt), false); // here action data won't be used

      locked l(_self, _self);
      l.emplace(from, [&](auto& o) {
         o.seq = seq;
         o.account = from;
         o.balance = quantity;
      })

      // TODO: permission
      INLINE_ACTION_SENDER(eosio::icp, sendaction)(_icp.icp, {_self, N(active)}, {seq, pack(icp_send), expiration}, pack(icp_receive));
   }

   void token::icpfrom(account_name icp_from, account_name to, asset quantity, string memo) {
      require_auth(_self);

      eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

      mint(to, quantity);
   }

   void token::icpreceipt(uint64_t seq, receipt_status status, bytes data) {
      require_auth(_self);

      locked l(_self, _self);
      auto it = l.find(seq);
      if (it != l.end()) {
         if (status == receipt_status::expired) { // icp transfer transaction expired or failed
            transfer(_self, it->account, it->balance, "icp release locked asset");
         }

         l.erase(it);
      }
   }

   void token::icptransfer(account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration) {
      require_auth(from);

      deposits dps(_self, from);
      const auto &dp = dps.get(quantity.symbol.name(), "no deposit object found");
      eosio_assert(dp.balance.amount >= quantity.amount, "overdrawn balance");

      if (dp.balance.amount == quantity.amount) {
         dps.erase(dp);
      } else {
         dps.modify(dp, from, [&](auto &a) {
            a.balance -= value;
         })
      }

      icpto(from, icp_to, quantity, memo, expiration); // TODO: original memo?
   }

   void token::apply_transfer(account_name from, account_name to, asset quantity, string memo) {
      // only care about token receiving
      if (to != _self) {
         return;
      }

      if (memo.find("icp ") == 0) { // it is an icp call
         auto account_end = memo.find(' ', 4);
         eosio_assert(account_end != std::string::npos, "invalid icp token transfer memo");
         auto n = memo.substr(4, account_end - 4);
         auto icp_to = eosio::string_to_name(n.c_str());
         auto h = memo.substr(account_end + 1);
         auto icp_expiration = static_cast<uint32_t>(std::stoul(h));

         icpto(from, icp_to, quantity, memo, icp_expiration); // TODO: original memo?

      } else { // deposit
         deposits dps(_self, from);
         auto dp = dps.find(quantity.symbol.name());
         if (dp == dps.end()) {
            dps.emplace(from, [&](auto &a) {
               a.balance = quantity
            });
         } else {
            dps.modify(dp, 0, [&](auto &a) {
               a.balance += value;
            });
         }
      }
   }

   void token::mint(account_name to, asset quantity) {
      require_auth(_self);

      eosio_assert(quantity.is_valid(), "invalid quantity");
      eosio_assert(quantity.amount > 0, "must mint positive quantity");

      auto sym_name = sym.name();
      stats statstable(_self, sym_name);
      auto existing = statstable.find(sym_name);
      eosio_assert(existing != statstable.end(), "token with symbol does not exist, create token before mint");
      const auto &st = *existing;

      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(quantity.amount <= std::numeric_limits<int64_t>::max() - st.supply.amount, "quantity exceeds available supply");

      statstable.modify(st, 0, [&](auto &s) {
         s.supply += quantity;
      });

      add_balance(to, quantity, to); // account `to` as ram payer
   }

   void token::burn(account_name to, asset quantity) {
      require_auth(_self);

      eosio_assert( quantity.is_valid(), "invalid quantity" );
      eosio_assert( quantity.amount > 0, "must burn positive quantity" );

      auto sym_name = sym.name();
      stats statstable(_self, sym_name);
      auto existing = statstable.find(sym_name);
      eosio_assert(existing != statstable.end(), "token with symbol does not exist, create token before burn");
      const auto &st = *existing;

      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(quantity.amount <= st.supply.amount, "quantity exceeds available supply");

      statstable.modify(st, 0, [&](auto &s) {
         s.supply -= quantity;
      });

      sub_balance(to, quantity);
   }

   void token::transfer(account_name from, account_name to, asset quantity, string memo) {
      eosio_assert(from != to, "cannot transfer to self");
      require_auth(from);
      eosio_assert(is_account(to), "to account does not exist");
      auto sym = quantity.symbol.name();
      stats statstable(_self, sym);
      const auto &st = statstable.get(sym);

      require_recipient(from);
      require_recipient(to);

      eosio_assert(quantity.is_valid(), "invalid quantity");
      eosio_assert(quantity.amount > 0, "must transfer positive quantity");
      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

      sub_balance(from, quantity);
      add_balance(to, quantity, from);
   }

   void token::sub_balance(account_name owner, asset value) {
      accounts from_acnts(_self, owner);

      const auto &from = from_acnts.get(value.symbol.name(), "no balance object found");
      eosio_assert(from.balance.amount >= value.amount, "overdrawn balance");

      if (from.balance.amount == value.amount) {
         from_acnts.erase(from);
      } else {
         from_acnts.modify(from, owner, [&](auto &a) {
            a.balance -= value;
         });
      }
   }

   void token::add_balance(account_name owner, asset value, account_name ram_payer) {
      accounts to_acnts(_self, owner);
      auto to = to_acnts.find(value.symbol.name());
      if (to == to_acnts.end()) {
         to_acnts.emplace(ram_payer, [&](auto &a) {
            a.balance = value;
         });
      } else {
         to_acnts.modify(to, 0, [&](auto &a) {
            a.balance += value;
         });
      }
   }

}

extern "C" {
   void apply(uint64_t receiver, uint64_t code, uint64_t action) {
      auto self = receiver;
      if (action == N(onerror)) {
         /* onerror is only valid if it is for the "eosio" code account and authorized by "eosio"'s "active permission */
         eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account");
      }
      if (code == self || action == N(onerror)) {
         icp::token thiscontract(self);
         switch (action) {
            EOSIO_API(icp::token, (seticp)(setpeer)(create)(icptransfer)(icpfrom)(transfer))
         }
      }
      if (code == N(eosio.token) && action == N(transfer)) {
         icp::token thiscontract(self);
         switch (action) {
            EOSIO_API(icp::token, (apply_transfer))
         }
      }
   }
}

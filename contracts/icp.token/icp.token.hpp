#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

namespace icp {

   using namespace eosio;

   class token : public contract {
   public:
      token(account_name self);

      void seticp(acccount_name icp);
      void setpeer(account_name peer);

      void create(account_name issuer, symbol_name symbol);

      void icptransfer(account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration);

      void icpfrom(account_name icp_from, account_name to, asset quantity, string memo);

      void icpreceipt(uint64_t seq, receipt_status status, bytes data);

      void transfer(account_name from, account_name to, asset quantity, string memo);

      void apply_transfer(account_name from, account_name to, asset quantity, string memo);

   private:
      void icpto(account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration);

      void mint(account_name to, asset quantity);
      void burn(account_name to, asset quantity);

      struct icp_contract {
         account_name icp = 0;
      };

      struct peer_contract {
         account_name peer = 0;
      };

      struct account {
         asset    balance;

         uint64_t primary_key()const { return balance.symbol.name(); }
      };

      struct currency_stats {
         asset          supply;
         account_name   issuer;

         uint64_t primary_key()const { return supply.symbol.name(); }
      };

      struct account_deposit {
         asset balance;

         uint64_t primary_key()const { return balance.symbol.name(); }
      };

      struct account_locked {
         uint64_t seq;
         account_name account;
         asset balance;

         uint64_t primary_key()const { return seq; }
      };

      typedef eosio::singleton<N(icp), icp_contract> icp_singleton;
      typedef eosio::singleton<N(peer), peer_contract> peer_singleton;
      typedef eosio::multi_index<N(accounts), account> accounts;
      typedef eosio::multi_index<N(stat), currency_stats> stats;
      typedef eosio::multi_index<N(deposit), account_deposit> deposits;
      typedef eosio::multi_index<N(locked), account_locked> locked;

      void sub_balance( account_name owner, asset value );
      void add_balance( account_name owner, asset value, account_name ram_payer );

      icp_contract _icp;
      peer_contract _peer;
   };

}

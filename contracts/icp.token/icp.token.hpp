#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>

namespace icp {

   using namespace std;
   using namespace eosio;

   class token : public contract {
   public:
      token(account_name self);

      [[eosio::action]]
      void setcontracts(account_name icp, account_name peer);

      // APIs for token asset transferred from peer chain
      [[eosio::action]]
      void create(account_name contract, string symbol);
      [[eosio::action]]
      void transfer(account_name contract, account_name from, account_name to, asset quantity, string memo);

      /** Receive asset transfer from peer contract.
       *
       * @param icp_from - the sender on the peer chain
       * @param to - the receiver on this chain
       * @param quantity
       * @param memo
       */
      [[eosio::action]]
      void icpreceive(account_name contract, account_name icp_from, account_name to, asset quantity, string memo, uint8_t refund);

      /**
       *
       * @param seq
       * @param status
       * @param data
       */
      [[eosio::action]]
      void icpreceipt(uint64_t seq, uint8_t status, bytes data);

      /** Applied when other token contract `transfer` to this contract.
       * The memo will be parsed for distinguish between transfer or deposit.
       * @param contract
       * @param from
       * @param to
       * @param quantity
       * @param memo
       */
      void icp_transfer_or_deposit(account_name contract, account_name from, account_name to, asset quantity, string memo);

      /** Transfer asset with icp.
       * The asset must have been deposited by `icp_transfer_or_deposit`.
       * @param from
       * @param icp_to
       * @param quantity
       * @param memo
       * @param expiration
       */
      [[eosio::action]]
      void icptransfer(account_name contract, account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration);

      [[eosio::action]]
      void icprefund(account_name contract, account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration);

   private:
      void sub_balance(account_name contract, account_name owner, asset value);
      void add_balance(account_name contract, account_name owner, asset value, account_name ram_payer);

      void icp_transfer(account_name contract, account_name from, account_name icp_to, asset quantity, string memo, uint32_t expiration, bool refund);

      void mint(account_name contract, account_name to, asset quantity);
      void burn(account_name contract, account_name from, asset quantity);

      static uint128_t account_asset_key(const account_name& account, const asset& balance) {
            return (uint128_t(account) << 64) + balance.symbol.name();
      }

      /** Collaborative contracts.
       * @param icp - the base icp contract on local chain
       * @param peer - the icp.token contract on peer chain
       */
      struct [[eosio::table]] collaborative_contract {
         account_name icp = 0;
         account_name peer = 0;
      };

      /** Asset account for transferred from peer chain.
       * @param scope - the token contract from peer chain
       * @param account - the owner
       * @param balance - the asset balance
       */
      struct [[eosio::table]] account {
         uint64_t pk;
         account_name account;
         asset    balance;

         auto primary_key()const { return pk; }
         uint128_t by_account_asset() const { return account_asset_key(account, balance); }
      };

      /** Asset account stats for transferred from peer chain.
       * @param scope - the token contract from peer chain
       * @param supply - the asset stats
       */
      struct [[eosio::table]] account_stats {
         asset supply;

         uint64_t primary_key()const { return supply.symbol.name(); }
      };

      /** Pre-deposit asset for future icp transfer.
       * @param scope - the token contract
       * @param account - the owner
       * @param balance - the transferred asset
       */
      struct [[eosio::table]] account_deposit {
         uint64_t pk;
         account_name account;
         asset balance;

         auto primary_key()const { return pk; }
         uint128_t by_account_asset() const { return account_asset_key(account, balance); }
      };

      /** Temporary locked asset for icp transfer.
       * If icp transfer failed (eg. expired), it will be released to the original sender.
       * Otherwise (eg. succeeded), it will just be erased, i.e., the asset will be kept by this contract.
       * @param scope - this contract
       * @param seq - the icp packet sequence
       * @param contract - the token contract
       * @param account - the sender
       * @param balance - the transferred asset
       */
      struct [[eosio::table]] account_locked {
         uint64_t seq;
         account_name contract;
         account_name account;
         asset balance;
         uint8_t refund;

         uint64_t primary_key()const { return seq; }
      };

      typedef eosio::singleton<N(co), collaborative_contract> co_singleton;
      typedef eosio::multi_index<N(accounts), account,
         indexed_by<N(accountasset), const_mem_fun<account, uint128_t, &account::by_account_asset>>
      > accounts;
      typedef eosio::multi_index<N(stat), account_stats> stats;
      typedef eosio::multi_index<N(deposit), account_deposit,
         indexed_by<N(accountasset), const_mem_fun<account_deposit, uint128_t, &account_deposit::by_account_asset>>
      > deposits;
      typedef eosio::multi_index<N(locked), account_locked> locked;

      collaborative_contract _co;
   };

}

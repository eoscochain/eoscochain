#include "eosio.system.hpp"
#include <eosiolib/dispatcher.hpp>

#include "producer_pay.cpp"
#include "delegate_bandwidth.cpp"
#include "voting.cpp"
#include "exchange_state.cpp"


namespace eosiosystem {

   uint64_t system_token_symbol() {
     static auto symbol =  core_symbol();
     return symbol;
   }

   system_contract::system_contract( account_name s )
   :native(s),
    _voters(_self,_self),
    _producers(_self,_self),
    _global(_self,_self),
    _rammarket(_self,_self)
   {
      //print( "construct system\n" );
      _gstate = _global.exists() ? _global.get() : get_default_parameters();

      auto itr = _rammarket.find(S(4,RAMCORE));

      if( itr == _rammarket.end() ) {
         auto system_token_supply   = eosio::token(N(eosio.token)).get_supply(eosio::symbol_type(system_token_symbol()).name()).amount;
         if( system_token_supply > 0 ) {
            itr = _rammarket.emplace( _self, [&]( auto& m ) {
               m.supply.amount = 100000000000000ll;
               m.supply.symbol = S(4,RAMCORE);
               m.base.balance.amount = int64_t(_gstate.free_ram());
               m.base.balance.symbol = S(0,RAM);
               m.quote.balance.amount = system_token_supply / 1000;
               m.quote.balance.symbol = core_symbol();
            });
         }
      } else {
         //print( "ram market already created" );
      }
   }

   eosio_global_state system_contract::get_default_parameters() {
      eosio_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }


   system_contract::~system_contract() {
      //print( "destruct system\n" );
      _global.set( _gstate, _self );
      //eosio_exit(0);
   }

   void system_contract::setram( uint64_t max_ram_size ) {
      require_auth( _self );

      eosio_assert( _gstate.max_ram_size < max_ram_size, "ram may only be increased" ); /// decreasing ram might result market maker issues
      eosio_assert( max_ram_size < 1024ll*1024*1024*1024*1024, "ram size is unrealistic" );
      eosio_assert( max_ram_size > _gstate.total_ram_bytes_reserved, "attempt to set max below reserved" );

      auto delta = int64_t(max_ram_size) - int64_t(_gstate.max_ram_size);
      auto itr = _rammarket.find(S(4,RAMCORE));

      /**
       *  Increase or decrease the amount of ram for sale based upon the change in max
       *  ram size.
       */
      _rammarket.modify( itr, 0, [&]( auto& m ) {
         m.base.balance.amount += delta;
      });

      _gstate.max_ram_size = max_ram_size;
      _global.set( _gstate, _self );
   }

   void system_contract::setparams( const eosio::blockchain_parameters& params ) {
      require_auth( N(eosio) );
      (eosio::blockchain_parameters&)(_gstate) = params;
      eosio_assert( 3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3" );
      set_blockchain_parameters( params );
   }

   void system_contract::setpriv( account_name account, uint8_t ispriv ) {
      require_auth( _self );
      set_privileged( account, ispriv );
   }

   void system_contract::rmvproducer( account_name producer ) {
      require_auth( _self );
      auto prod = _producers.find( producer );
      eosio_assert( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, 0, [&](auto& p) {
            p.deactivate();
         });
   }

   void system_contract::bidname( account_name bidder, account_name newname, asset bid ) {
      require_auth( bidder );
      eosio_assert( eosio::name_suffix(newname) == newname, "you can only bid on top-level suffix" );
      eosio_assert( newname != 0, "the empty name is not a valid account name to bid on" );
      eosio_assert( (newname & 0xFull) == 0, "13 character names are not valid account names to bid on" );
      eosio_assert( (newname & 0x1F0ull) == 0, "accounts with 12 character names and no dots can be created without bidding required" );
      eosio_assert( !is_account( newname ), "account already exists" );
      eosio_assert( bid.symbol == asset().symbol, "asset must be system token" );
      eosio_assert( bid.amount > 0, "insufficient bid" );

      INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {bidder,N(active)},
                                                    { bidder, N(eosio.names), bid, std::string("bid name ")+(name{newname}).to_string()  } );

      name_bid_table bids(_self,_self);
      print( name{bidder}, " bid ", bid, " on ", name{newname}, "\n" );
      auto current = bids.find( newname );
      if( current == bids.end() ) {
         bids.emplace( bidder, [&]( auto& b ) {
            b.newname = newname;
            b.high_bidder = bidder;
            b.high_bid = bid.amount;
            b.last_bid_time = current_time();
         });
      } else {
         eosio_assert( current->high_bid > 0, "this auction has already closed" );
         eosio_assert( bid.amount - current->high_bid > (current->high_bid / 10), "must increase bid by 10%" );
         eosio_assert( current->high_bidder != bidder, "account is already highest bidder" );

         INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {N(eosio.names),N(active)},
                                                       { N(eosio.names), current->high_bidder, asset(current->high_bid),
                                                       std::string("refund bid on name ")+(name{newname}).to_string()  } );

         bids.modify( current, bidder, [&]( auto& b ) {
            b.high_bidder = bidder;
            b.high_bid = bid.amount;
            b.last_bid_time = current_time();
         });
      }
   }

   void system_contract::setglobal( std::string name, std::string value ) {
      require_auth( _self );

      eosio_assert( _gstate.total_activated_stake < _gstate.min_activated_stake, "minimum activated stake has reached" );

      if (name == "max_producer_schedule_size") {
         auto sched_size = std::stoi(value);

         eosio_assert( sched_size >= 3 && sched_size <= 51, "producers number must be in range [3, 51]" ); // TODO
         eosio_assert( sched_size % 2 == 1, "producers number must be odd" );

         _gstate.max_producer_schedule_size = static_cast<uint8_t>(sched_size);
      } else if (name == "min_pervote_daily_pay") {
         auto min_vpay = std::stoll(value);

         eosio_assert( min_vpay >= 0 && min_vpay <= 100'0000, "minimum pervote daily pay must be in range [0, 100'0000]" ); // TODO

         _gstate.min_pervote_daily_pay = min_vpay;
      } else if ( name == "min_activated_stake") {
         auto min_activated_stake = std::stoll(value);

         eosio_assert( min_activated_stake >= 0 && min_activated_stake <= 150'000'000'0000, "minimum activated stake must be in range [0, 150'000'000'0000]" ); // TODO

         _gstate.min_activated_stake = min_activated_stake;
      } else if ( name == "continuous_rate") {
         auto rate = std::stod(value);

         eosio_assert( rate >= 0 && rate <= 1, "continuous rate must be in range [0, 1]" ); // TODO

         _gstate.continuous_rate = rate;
      } else if ( name == "to_producers_rate") {
         auto rate = std::stod(value);

         eosio_assert( rate >= 0 && rate <= 1, "to_producers_rate must be in range [0, 1]" ); // TODO

         _gstate.to_producers_rate = rate;
      } else if ( name == "to_bpay_rate") {
         auto rate = std::stod(value);

         eosio_assert( rate >= 0 && rate <= 1, "to_bpay_rate must be in range [0, 1]" ); // TODO

         _gstate.to_bpay_rate = rate;
      } else if ( name == "refund_delay" ) {
         auto refund_delay = std::stoul(value);

         eosio_assert(refund_delay >= 0 && refund_delay <= std::numeric_limits<uint32_t>::max(), "refund_delay must be uint32_t");

         _gstate.refund_delay = static_cast<time>(refund_delay);
      }
   }

   void system_contract::setgmr(int64_t ram_byte, int64_t cpu_us, int64_t net_byte){
      set_guaranteed_minimum_resources(ram_byte, cpu_us, net_byte);
   }

   /**
    *  Called after a new account is created. This code enforces resource-limits rules
    *  for new accounts as well as new account naming conventions.
    *
    *  Account names containing '.' symbols must have a suffix equal to the name of the creator.
    *  This allows users who buy a premium name (shorter than 12 characters with no dots) to be the only ones
    *  who can create accounts with the creator's name as a suffix.
    *
    */
   void native::newaccount( account_name     creator,
                            account_name     newact
                            /*  no need to parse authorities
                            const authority& owner,
                            const authority& active*/ ) {

      if( creator != _self ) {
         auto tmp = newact >> 4;
         bool has_dot = false;

         for( uint32_t i = 0; i < 12; ++i ) {
           has_dot |= !(tmp & 0x1f);
           tmp >>= 5;
         }
         if( has_dot ) { // or is less than 12 characters
            auto suffix = eosio::name_suffix(newact);
            if( suffix == newact ) {
               name_bid_table bids(_self,_self);
               auto current = bids.find( newact );
               eosio_assert( current != bids.end(), "no active bid for name" );
               eosio_assert( current->high_bidder == creator, "only highest bidder can claim" );
               eosio_assert( current->high_bid < 0, "auction for name is not closed yet" );
               bids.erase( current );
            } else {
               eosio_assert( creator == suffix, "only suffix may create this account" );
            }
         }
      }

      user_resources_table  userres( _self, newact);

      userres.emplace( newact, [&]( auto& res ) {
        res.owner = newact;
      });

      set_resource_limits( newact, 0, 0, 0 );
   }

} /// eosio.system


EOSIO_ABI( eosiosystem::system_contract,
     // native.hpp (newaccount definition is actually in eosio.system.cpp)
     (newaccount)(updateauth)(deleteauth)(linkauth)(unlinkauth)(canceldelay)(onerror)
     // eosio.system.cpp
     (setram)(setparams)(setpriv)(rmvproducer)(bidname)(setglobal)(setgmr)
     // delegate_bandwidth.cpp
     (buyrambytes)(buyram)(sellram)(delegatebw)(undelegatebw)(refund)
     // voting.cpp
     (regproducer)(unregprod)(voteproducer)(regproxy)
     // producer_pay.cpp
     (onblock)(claimrewards)
)

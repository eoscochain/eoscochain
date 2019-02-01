#include "exchange_state.hpp"

namespace kafka {
   asset exchange_state::convert_to_exchange( connector& c, asset in ) {

      real_type R(supply.get_amount());
      real_type C(c.balance.get_amount() + in.get_amount());
      real_type F(c.weight/1000.0);
      real_type T(in.get_amount());
      real_type ONE(1.0);

      real_type E = -R * (ONE - std::pow( ONE + T / C, F) );
      //print( "E: ", E, "\n");
      int64_t issued = int64_t(E);

      supply += asset(issued, supply.get_symbol());
      c.balance += in;

      return asset( issued, supply.get_symbol() );
   }

   asset exchange_state::convert_from_exchange( connector& c, asset in ) {
      EOS_ASSERT( in.get_symbol() == supply.get_symbol(), chain::contract_exception, "unexpected asset symbol input" );

      real_type R((supply - in).get_amount());
      real_type C(c.balance.get_amount());
      real_type F(1000.0/c.weight);
      real_type E(in.get_amount());
      real_type ONE(1.0);


     // potentially more accurate: 
     // The functions std::expm1 and std::log1p are useful for financial calculations, for example, 
     // when calculating small daily interest rates: (1+x)n
     // -1 can be expressed as std::expm1(n * std::log1p(x)). 
     // real_type T = C * std::expm1( F * std::log1p(E/R) );
      
      real_type T = C * (std::pow( ONE + E/R, F) - ONE);
      //print( "T: ", T, "\n");
      int64_t out = int64_t(T);

      supply -= in;
      c.balance -= asset(out, c.balance.get_symbol());

      return asset( out, c.balance.get_symbol() );
   }

   asset exchange_state::convert( asset from, symbol to ) {
      auto sell_symbol  = from.get_symbol();
      auto ex_symbol    = supply.get_symbol();
      auto base_symbol  = base.balance.get_symbol();
      auto quote_symbol = quote.balance.get_symbol();

      //print( "From: ", from, " TO ", asset( 0,to), "\n" );
      //print( "base: ", base_symbol, "\n" );
      //print( "quote: ", quote_symbol, "\n" );
      //print( "ex: ", supply.symbol, "\n" );

      if( sell_symbol != ex_symbol ) {
         if( sell_symbol == base_symbol ) {
            from = convert_to_exchange( base, from );
         } else if( sell_symbol == quote_symbol ) {
            from = convert_to_exchange( quote, from );
         } else { 
            EOS_ASSERT( false, chain::contract_exception, "invalid sell" );
         }
      } else {
         if( to == base_symbol ) {
            from = convert_from_exchange( base, from ); 
         } else if( to == quote_symbol ) {
            from = convert_from_exchange( quote, from ); 
         } else {
            EOS_ASSERT( false, chain::contract_exception, "invalid conversion" );
         }
      }

      if( to != from.get_symbol() )
         return convert( from, to );

      return from;
   }
   
}

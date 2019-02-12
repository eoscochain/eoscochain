#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/testing/chainbase_fixture.hpp>
#include <eosio/chain/resource_limits_private.hpp>

#include <algorithm>
#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif

using namespace eosio::chain::resource_limits;
using namespace eosio::testing;
using namespace eosio::chain;

using mvo = fc::mutable_variant_object;

class gmr_fixture : private chainbase_fixture<512 * 1024>, public resource_limits_manager
{
 public:
   gmr_fixture()
       : chainbase_fixture(), resource_limits_manager(*chainbase_fixture::_db)
   {
      add_indices();
      initialize_database();
   }

   ~gmr_fixture() {}

   chainbase::database::session start_session()
   {
      return chainbase_fixture::_db->start_undo_session(true);
   }
};

BOOST_AUTO_TEST_SUITE(gmr_test)


BOOST_FIXTURE_TEST_CASE(check_gmr_parameters, gmr_fixture)
try
{
   auto gmr = get_gmr_parameters();
   BOOST_TEST(gmr.cpu_us == config::default_gmr_cpu_limit);
   BOOST_TEST(gmr.net_byte == config::default_gmr_net_limit);
   BOOST_TEST(gmr.ram_byte == config::default_gmr_ram_limit);


   set_gmr_parameters(
   {  1024, 200000,10240}
   );
   gmr = get_gmr_parameters();
   BOOST_TEST(gmr.cpu_us == 200000);
   BOOST_TEST(gmr.net_byte == 10240);
   BOOST_TEST(gmr.ram_byte == 1024);

   set_gmr_parameters(
   {  3, 3, 3}
   );
   gmr = get_gmr_parameters();
   BOOST_TEST(gmr.cpu_us == 3);
   BOOST_TEST(gmr.net_byte == 3);
   BOOST_TEST(gmr.ram_byte == 3);
}
FC_LOG_AND_RETHROW();

BOOST_FIXTURE_TEST_CASE(check_block_limits_cpu_lowerthan, gmr_fixture)
try
{
   const account_name account(1);
   const uint64_t increment = 10000;
   initialize_account(account);
   set_account_limits(account, 1000, 0, 0);
   initialize_account(N(dan));
   initialize_account(N(everyone));
   set_account_limits(N(dan), 0, 0, 10000);
   set_account_limits(N(everyone), 0, 0, 10000000000000ll);

   process_account_limit_updates();

   BOOST_TEST(get_account_cpu_limit(account) == config::default_gmr_cpu_limit);

   const uint64_t expected_iterations = config::default_gmr_cpu_limit / increment;

   for (int idx = 0; idx < expected_iterations-1; idx++)
   {
//      BOOST_TEST_MESSAGE("get_account_cpu_limit " << get_account_cpu_limit(account) );
      add_transaction_usage({account}, increment, 0, 0);
      process_block_usage(idx);
   }

   auto arl = get_account_cpu_limit_ex(account, true);

   BOOST_TEST(arl.available >= 9997);
//   BOOST_TEST_MESSAGE("arl.available " << arl.available << "  arl.used " <<  arl.used << " arl.max " << arl.max);
//   BOOST_REQUIRE_THROW(add_transaction_usage({account}, increment/2, 0, 0), block_resource_exhausted);
}
FC_LOG_AND_RETHROW();

BOOST_FIXTURE_TEST_CASE(check_block_limits_net_lowerthan, gmr_fixture)
try
{
   const account_name account(1);
   const uint64_t increment = 1024;
   initialize_account(account);
   set_account_limits(account, increment, 0, 0);
   initialize_account(N(dan));
   initialize_account(N(everyone));
   set_account_limits(N(dan), 0, 10000, 0);
   set_account_limits(N(everyone), 0, 10000000000000ll, 0);
   process_account_limit_updates();

   const uint64_t expected_iterations = config::default_gmr_net_limit / increment;

   for (int idx = 0; idx < expected_iterations-1; idx++)
   {
//      BOOST_TEST_MESSAGE("get_account_net_limit " << get_account_net_limit(account) );
      add_transaction_usage({account}, 0, increment, 0);
      process_block_usage(idx);
   }

   auto arl = get_account_net_limit_ex(account, true);
   BOOST_TEST(arl.available >= 1023);
//   BOOST_TEST_MESSAGE("get_account_net_limit " << get_account_net_limit(account) );

   // BOOST_REQUIRE_THROW(add_transaction_usage({account},  0,increment, 0), block_resource_exhausted);
}
FC_LOG_AND_RETHROW();

BOOST_FIXTURE_TEST_CASE(check_block_limits_ram, gmr_fixture)
try
{
    set_gmr_parameters(
         {  1024, 200000,10240}
      );

   const account_name account(1);
   const uint64_t increment = 1000;
   initialize_account(account);
   set_account_limits(account, increment, 10, 10);
   initialize_account(N(dan));
   initialize_account(N(everyone));
   set_account_limits(N(dan), 0, 10000, 0);
   set_account_limits(N(everyone), 0, 10000000000000ll, 0);
   process_account_limit_updates();


   int64_t ram_bytes;
   int64_t net_weight;
   int64_t cpu_weight;
   bool raw = false;
   get_account_limits(account, ram_bytes, net_weight, cpu_weight, raw);

   BOOST_TEST(increment + config::default_gmr_ram_limit == ram_bytes);
   BOOST_TEST(10 == net_weight);
   BOOST_TEST(10 == cpu_weight);


    raw = true;
   get_account_limits(account, ram_bytes, net_weight, cpu_weight, raw);

   BOOST_TEST(increment == ram_bytes);
   BOOST_TEST(10 == net_weight);
   BOOST_TEST(10 == cpu_weight);

   // BOOST_REQUIRE_THROW(add_transaction_usage({account},  0,increment, 0), block_resource_exhausted);
}
FC_LOG_AND_RETHROW();



BOOST_FIXTURE_TEST_CASE(get_account_limits_res, gmr_fixture)
try
{
   const account_name account(1);
   const uint64_t increment = 1000;
   initialize_account(account);
   set_account_limits(account, increment+24, 0, 0);
   initialize_account(N(dan));
   initialize_account(N(everyone));
   set_account_limits(N(dan), 0, 10000, 0);
   set_account_limits(N(everyone), 0, 10000000000000ll, 0);
   process_account_limit_updates();

   const uint64_t expected_iterations = config::default_gmr_net_limit / increment;

   for (int idx = 0; idx < expected_iterations-1; idx++)
   {
      add_transaction_usage({account}, 0, increment, 0);
      process_block_usage(idx);
   }

   auto arl = get_account_net_limit_ex(account, true);
   BOOST_TEST(arl.available > 0);

   int64_t ram_bytes;
   int64_t net_weight;
   int64_t cpu_weight;
   bool raw = false;
   get_account_limits(account, ram_bytes, net_weight, cpu_weight, raw);

   BOOST_TEST(1024 == ram_bytes);
   BOOST_TEST(0 == net_weight);
   BOOST_TEST(0 == cpu_weight);


    raw = true;
   get_account_limits(account, ram_bytes, net_weight, cpu_weight, raw);

   BOOST_TEST(1024 == ram_bytes);
   BOOST_TEST(0 == net_weight);
   BOOST_TEST(0 == cpu_weight);


   // BOOST_REQUIRE_THROW(add_transaction_usage({account},  0,increment, 0), block_resource_exhausted);
}
FC_LOG_AND_RETHROW();


//BOOST_FIXTURE_TEST_CASE(check_setgmr_api, TESTER)
//try
//{
//   BOOST_TEST_MESSAGE("1 ... ");
//   push_action( config::system_account_name, N(setgmr), config::system_account_name, mvo()( "ram_byte", 1024)("cpu_us",200000)("net_byte",10240) );
//   BOOST_TEST_MESSAGE("2 ... ");
//   const auto config = control->db().get<eosio::chain::resource_limits::gmr_config_object>();
//
//   BOOST_TEST_MESSAGE("config ... ");
//   const auto& gmr = config.res_parameters;
//   BOOST_TEST(gmr.cpu_us == 200000);
//   BOOST_TEST(gmr.net_byte == 10240);
//   BOOST_TEST(gmr.ram_byte == 1024);
//}
//FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()

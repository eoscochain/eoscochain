#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>

#include "cdt.version.contract/cdt.contracts.hpp"
//#include <eosio.system/eosio.system.wast.hpp>
//#include <eosio.system/eosio.system.abi.hpp>
//// These contracts are still under dev
//#include <eosio.token/eosio.token.wast.hpp>
//#include <eosio.token/eosio.token.abi.hpp>
//#include <eosio.msig/eosio.msig.wast.hpp>
//#include <eosio.msig/eosio.msig.abi.hpp>

#include <Runtime/Runtime.h>

#include <fc/variant_object.hpp>

#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif


using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

struct genesis_account {
    account_name aname;
    uint64_t     initial_balance;
};

std::vector<genesis_account> test_genesis2( {
      {N(b1),       100'000'000'0000ll},
      {N(whale4),    40'001'000'0000ll},
      {N(whale3),    29'998'000'0000ll},
      {N(whale2),    20'001'000'0000ll},
      {N(proda),      1'000'000'0000ll},
      {N(prodb),      1'000'000'0000ll},
      {N(prodc),      1'000'000'0000ll},
      {N(prodd),      1'000'000'0000ll},
      {N(prode),      1'000'000'0000ll},
      {N(prodf),      1'000'000'0000ll},
      {N(prodg),      1'000'000'0000ll},
      {N(prodh),      1'000'000'0000ll},
      {N(prodi),      1'000'000'0000ll},
      {N(prodj),      1'000'000'0000ll},
      {N(prodk),      1'000'000'0000ll},
      {N(prodl),      1'000'000'0000ll},
      {N(prodm),      1'000'000'0000ll},
      {N(prodn),      1'000'000'0000ll},
      {N(prodo),      1'000'000'0000ll},
      {N(prodp),      1'000'000'0000ll},
      {N(prodq),      1'000'000'0000ll},
      {N(prodr),      1'000'000'0000ll},
      {N(prods),      1'000'000'0000ll},
      {N(prodt),      1'000'000'0000ll},
      {N(produ),      1'000'000'0000ll},
      {N(runnerup1),  1'000'000'0000ll},
      {N(runnerup2),  1'000'000'0000ll},
      {N(runnerup3),  1'000'000'0000ll},
      {N(minow1),        100'0000ll},
      {N(minow2),          1'0000ll},
      {N(minow3),          1'0000ll},
      {N(masses),799'998'999'0000ll},
      {N(proxy),       1'001'0000ll}
});

auto producer_candidates = {
        N(proda), N(prodb), N(prodc), N(prodd), N(prode), N(prodf), N(prodg),
        N(prodh), N(prodi), N(prodj), N(prodk), N(prodl), N(prodm), N(prodn),
        N(prodo), N(prodp), N(prodq), N(prodr), N(prods), N(prodt), N(produ),
        N(runnerup1), N(runnerup2), N(runnerup3)
};

class voter_bonus_tester : public TESTER {
public:

    fc::variant get_global_state() {
       vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, N(global), N(global) );
       if (data.empty()) std::cout << "\nData is empty\n" << std::endl;
       return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "eosio_global_state", data, abi_serializer_max_time );

    }

    fc::variant get_voter_bonus(account_name p) {
       vector<char> data = get_row_by_account( config::system_account_name, config::system_account_name, N(voterbonus), p );
       return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "voter_bonus", data, abi_serializer_max_time );
    }

    auto buyram( name payer, name receiver, asset ram ) {
       auto r = base_tester::push_action(config::system_account_name, N(buyram), payer, mvo()
               ("payer", payer)
               ("receiver", receiver)
               ("quant", ram)
       );
       produce_block();
       return r;
    }

    auto delegate_bandwidth( name from, name receiver, asset net, asset cpu, uint8_t transfer = 1) {
       auto r = base_tester::push_action(config::system_account_name, N(delegatebw), from, mvo()
               ("from", from )
               ("receiver", receiver)
               ("stake_net_quantity", net)
               ("stake_cpu_quantity", cpu)
               ("transfer", transfer)
       );
       produce_block();
       return r;
    }

    void create_currency( name contract, name manager, asset maxsupply, const private_key_type* signer = nullptr ) {
       auto act =  mutable_variant_object()
               ("issuer",       manager )
               ("maximum_supply", maxsupply );

       base_tester::push_action(contract, N(create), contract, act );
    }

    auto issue( name contract, name manager, name to, asset amount ) {
       auto r = base_tester::push_action( contract, N(issue), manager, mutable_variant_object()
               ("to",      to )
               ("quantity", amount )
               ("memo", "")
       );
       produce_block();
       return r;
    }

    auto transfer( name from, name to, asset amount, name contract = N(eosio.token) ) {
       auto r = base_tester::push_action( contract, N(transfer), from, mutable_variant_object()
               ("from", from)
               ("to",      to )
               ("quantity", amount )
               ("memo", "")
       );
       produce_block();
       return r;
    }

    auto claim_rewards( name owner ) {
       auto r = base_tester::push_action( config::system_account_name, N(claimrewards), owner, mvo()("owner",  owner ));
       produce_block();
       return r;
    }

    auto claim_bonus( name owner ) {
       auto r = base_tester::push_action( config::system_account_name, N(claimbonus), owner, mvo()("owner",  owner ));
       produce_block();
       return r;
    }

    auto set_privileged( name account ) {
       auto r = base_tester::push_action(config::system_account_name, N(setpriv), config::system_account_name,  mvo()("account", account)("is_priv", 1));
       produce_block();
       return r;
    }

    auto register_producer(name producer) {
       auto r = base_tester::push_action(config::system_account_name, N(regproducer), producer, mvo()
               ("producer",  name(producer))
               ("producer_key", get_public_key( producer, "active" ) )
               ("url", "" )
               ("location", 0 )
       );
       produce_block();
       return r;
    }


    auto undelegate_bandwidth( name from, name receiver, asset net, asset cpu ) {
       auto r = base_tester::push_action(config::system_account_name, N(undelegatebw), from, mvo()
               ("from", from )
               ("receiver", receiver)
               ("unstake_net_quantity", net)
               ("unstake_cpu_quantity", cpu)
       );
       produce_block();
       return r;
    }

    asset get_balance( const account_name& act ) {
       return get_currency_balance(N(eosio.token), symbol(CORE_SYMBOL), act);
    }

    void set_code_abi(const account_name& account, const vector<uint8_t>& wasm, const std::vector<char>& abi, const private_key_type* signer = nullptr) {
       wdump((account));
       set_code(account, wasm, signer);
       set_abi(account, abi.data(), signer);
       if (account == config::system_account_name) {
          const auto& accnt = control->db().get<account_object,by_name>( account );
          abi_def abi_definition;
          BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi_definition), true);
          abi_ser.set_abi(abi_definition, abi_serializer_max_time);
       }
       produce_blocks();
    }

    // Vote for producers
    void votepro( account_name voter, vector<account_name> producers ) {
        std::sort( producers.begin(), producers.end() );
        base_tester::push_action(config::system_account_name, N(voteproducer), voter, mvo()
                ("voter",  name(voter))
                ("proxy", name(0) )
                ("producers", producers)
        );
    };

    void init() {
       // Create eosio.msig and eosio.token
       create_accounts({N(eosio.msig), N(eosio.token), N(eosio.ram), N(eosio.ramfee), N(eosio.stake), N(eosio.vpay), N(eosio.bpay), N(eosio.saving) });

       // Set code for the following accounts:
       //  - eosio (code: eosio.bios) (already set by tester constructor)
       //  - eosio.msig (code: eosio.msig)
       //  - eosio.token (code: eosio.token)
       set_code_abi(N(eosio.msig), eosio::testing::contracts::msig_wasm(), eosio::testing::contracts::msig_abi());//, &eosio_active_pk);
       set_code_abi(N(eosio.token), eosio::testing::contracts::token_wasm(), eosio::testing::contracts::token_abi()); //, &eosio_active_pk);

       // Set privileged for eosio.msig and eosio.token
       set_privileged(N(eosio.msig));
       set_privileged(N(eosio.token));

       // Verify eosio.msig and eosio.token is privileged
       const auto& eosio_msig_acc = get<account_object, by_name>(N(eosio.msig));
       BOOST_TEST(eosio_msig_acc.privileged == true);
       const auto& eosio_token_acc = get<account_object, by_name>(N(eosio.token));
       BOOST_TEST(eosio_token_acc.privileged == true);


       // Create SYS tokens in eosio.token, set its manager as eosio
       auto max_supply = core_from_string("10000000000.0000"); /// 1x larger than 1B initial tokens
       auto initial_supply = core_from_string("1000000000.0000"); /// 1x larger than 1B initial tokens
       create_currency(N(eosio.token), config::system_account_name, max_supply);
       // Issue the genesis supply of 1 billion SYS tokens to eosio.system
       issue(N(eosio.token), config::system_account_name, config::system_account_name, initial_supply);

       auto actual = get_balance(config::system_account_name);
       BOOST_REQUIRE_EQUAL(initial_supply, actual);

       // Create genesis accounts
       for( const auto& a : test_genesis2 ) {
          create_account( a.aname, config::system_account_name );
       }

       // Set eosio.system to eosio
       set_code_abi(config::system_account_name, eosio::testing::contracts::system_wasm(), eosio::testing::contracts::system_abi());
       // init
       base_tester::push_action(config::system_account_name, N(init), config::system_account_name, mvo()
               ("version",  0)
               ("core", "4,SYS" )
       );

       // Buy ram and stake cpu and net for each genesis accounts
       for( const auto& a : test_genesis2 ) {
          auto ib = a.initial_balance;
          auto ram = 1000;
          auto net = (ib - ram) / 2;
          auto cpu = ib - net - ram;

          auto r = buyram(config::system_account_name, a.aname, asset(ram));
          BOOST_REQUIRE( !r->except_ptr );

          r = delegate_bandwidth(N(eosio.stake), a.aname, asset(net), asset(cpu));
          BOOST_REQUIRE( !r->except_ptr );
       }

       // Register producers
       for( auto pro : producer_candidates ) {
          register_producer(pro);
       }

       votepro( N(b1), { N(proda), N(prodb), N(prodc), N(prodd), N(prode), N(prodf), N(prodg),
                         N(prodh), N(prodi), N(prodj), N(prodk), N(prodl), N(prodm), N(prodn),
                         N(prodo), N(prodp), N(prodq), N(prodr), N(prods), N(prodt)/*, N(produ)*/} );
       votepro( N(whale2), {N(runnerup1), N(runnerup2), N(runnerup3)} );
       votepro( N(whale3), {N(proda), N(prodb), N(prodc), N(prodd), N(prode)} );

       // Total Stakes = b1 + whale2 + whale3 stake = (100,000,000 - 1,000) + (20,000,000 - 1,000) + (30,000,000 - 1,000)
       BOOST_TEST(get_global_state()["total_activated_stake"].as<int64_t>() == 1499989997000);

       // No producers will be set, since the total activated stake is less than 150,000,000
       produce_blocks_for_n_rounds(2); // 2 rounds since new producer schedule is set when the first block of next round is irreversible
       auto active_schedule = control->head_block_state()->active_schedule;
       BOOST_TEST(active_schedule.producers.size() == 1);
       BOOST_TEST(active_schedule.producers.front().producer_name == "eosio");

       // Spend some time so the producer pay pool is filled by the inflation rate
       produce_min_num_of_blocks_to_spend_time_wo_inactive_prod(fc::seconds(30 * 24 * 3600)); // 30 days
       // Since the total activated stake is less than 150,000,000, it shouldn't be possible to claim rewards
       BOOST_REQUIRE_THROW(claim_rewards(N(runnerup1)), eosio_assert_message_exception);

       // This will increase the total vote stake by (40,000,000 - 1,000)
       votepro( N(whale4), {N(prodq), N(prodr), N(prods), N(prodt), /*N(produ)*/} );
       BOOST_TEST(get_global_state()["total_activated_stake"].as<int64_t>() == 1899999996000);

       // Since the total vote stake is more than 150,000,000, the new producer set will be set
       produce_blocks_for_n_rounds(2); // 2 rounds since new producer schedule is set when the first block of next round is irreversible
       active_schedule = control->head_block_state()->active_schedule;
       BOOST_REQUIRE(active_schedule.producers.size() == 21);
       BOOST_TEST(active_schedule.producers.at(0).producer_name == "proda");
       BOOST_TEST(active_schedule.producers.at(1).producer_name == "prodb");
       BOOST_TEST(active_schedule.producers.at(2).producer_name == "prodc");
       BOOST_TEST(active_schedule.producers.at(3).producer_name == "prodd");
       BOOST_TEST(active_schedule.producers.at(4).producer_name == "prode");
       BOOST_TEST(active_schedule.producers.at(5).producer_name == "prodf");
       BOOST_TEST(active_schedule.producers.at(6).producer_name == "prodg");
       BOOST_TEST(active_schedule.producers.at(7).producer_name == "prodh");
       BOOST_TEST(active_schedule.producers.at(8).producer_name == "prodi");
       BOOST_TEST(active_schedule.producers.at(9).producer_name == "prodj");
       BOOST_TEST(active_schedule.producers.at(10).producer_name == "prodk");
       BOOST_TEST(active_schedule.producers.at(11).producer_name == "prodl");
       BOOST_TEST(active_schedule.producers.at(12).producer_name == "prodm");
       BOOST_TEST(active_schedule.producers.at(13).producer_name == "prodn");
       BOOST_TEST(active_schedule.producers.at(14).producer_name == "prodo");
       BOOST_TEST(active_schedule.producers.at(15).producer_name == "prodp");
       BOOST_TEST(active_schedule.producers.at(16).producer_name == "prodq");
       BOOST_TEST(active_schedule.producers.at(17).producer_name == "prodr");
       BOOST_TEST(active_schedule.producers.at(18).producer_name == "prods");
       BOOST_TEST(active_schedule.producers.at(19).producer_name == "prodt");
//       BOOST_TEST(active_schedule.producers.at(20).producer_name == "produ");

    }

//    to_voter_bonus_rate
    void setglobal(std::string name, std::string value) {
       base_tester::push_action(config::system_account_name, N(setglobal), config::system_account_name, mvo()
               ("name",  name)
               ("value", value )
       );
    }

    asset claim_voter_bonus_of_time(account_name voter, fc::microseconds skip_time = fc::days(1)) {
       for (auto& p : producer_candidates) {
          claim_rewards(p);
       }
       produce_blocks();

       claim_bonus(voter);
       auto old = get_balance(voter);
       produce_block(skip_time);

       for (auto& p : producer_candidates) {
          claim_rewards(p);
       }
       claim_bonus(voter);
       produce_blocks();
       return get_balance(voter) - old;
    }

    bool asset_equal(asset a, asset b)  {
       a = a - b;
       auto one = asset(100); // 0.0100 SYS
       auto one_ = asset(-100);
       return (a >= one_ && a <= one);
    }

    bool float_equal(float a, float b) {
       a -= b;
       return (a >= -0.1 && a <=0.1);
    }


    abi_serializer abi_ser;
};

BOOST_AUTO_TEST_SUITE(voter_bonus_tests)

    BOOST_FIXTURE_TEST_CASE( bootseq_test, voter_bonus_tester ) {
       try {
          init();

          // Spend some time so the producer pay pool is filled by the inflation rate
          produce_min_num_of_blocks_to_spend_time_wo_inactive_prod(fc::seconds(30 * 24 * 3600)); // 30 days
          // Since the total activated stake is larger than 150,000,000, pool should be filled reward should be bigger than zero
          claim_rewards(N(runnerup1));
          BOOST_TEST(get_balance(N(runnerup1)).get_amount() > 0);

          const auto first_june_2018 = fc::seconds(1527811200); // 2018-06-01
          const auto first_june_2028 = fc::seconds(1843430400); // 2028-06-01
          // Ensure that now is yet 10 years after 2018-06-01 yet
          BOOST_REQUIRE(control->head_block_time().time_since_epoch() < first_june_2028);

          // This should thrown an error, since block one can only unstake all his stake after 10 years

          BOOST_REQUIRE_THROW(undelegate_bandwidth(N(b1), N(b1), core_from_string("49999500.0000"), core_from_string("49999500.0000")), eosio_assert_message_exception);

          // Skip 10 years
          produce_block(first_june_2028 - control->head_block_time().time_since_epoch());

          // Block one should be able to unstake all his stake now
          undelegate_bandwidth(N(b1), N(b1), core_from_string("49999500.0000"), core_from_string("49999500.0000"));

// TODO: Complete this test
       } FC_LOG_AND_RETHROW()
    }

BOOST_FIXTURE_TEST_CASE( voting_produces_revenue, voter_bonus_tester ) {
 try {
    init();

    const account_name w2 = N(whale2);
    const account_name w4 = N(whale4);
    const account_name pu = N(produ);
    const account_name vp = N(eosio.vpay);
    const account_name bp = N(eosio.bpay);

    votepro( w2, {pu} );
    votepro( w4, {pu} );
    produce_blocks(1);
    BOOST_REQUIRE_THROW(claim_bonus(w2), eosio_assert_message_exception);


    for (float vRate = 0.1; vRate <= 1.0; vRate += 0.1) {
       setglobal("to_voter_bonus_rate", std::to_string(uint32_t(vRate * 1e6)));
       produce_blocks(1);

       auto oldu = get_balance(pu);
       auto old2 = get_balance(w2);
       auto old4 = get_balance(w4);
       auto oldv = get_balance(vp);
       auto oldb = get_balance(bp);

       produce_block(fc::days(1));

       for (auto &p : producer_candidates) {
          claim_rewards(p);
       }

       claim_bonus(N(b1));
       claim_bonus(N(whale3));
       claim_bonus(w2);
       claim_bonus(w4);

//       BOOST_TEST_MESSAGE("===4===vpay==" << get_balance(vp) );
//       BOOST_TEST_MESSAGE("======bpay==" << get_balance(bp) );
//       BOOST_TEST_MESSAGE("======w2==" << get_balance(w2) );
//       BOOST_TEST_MESSAGE("======w4==" << get_balance(w4) );

       auto addu = float(get_balance(pu).get_amount() - oldu.get_amount());
       auto add2 = float(get_balance(w2).get_amount() - old2.get_amount());
       auto add4 = float(get_balance(w4).get_amount() - old4.get_amount());
       auto addb = float(get_balance(bp).get_amount() - oldb.get_amount());
       auto addv = float(get_balance(vp).get_amount() - oldv.get_amount());
//
//       BOOST_TEST_MESSAGE("==== to_voter_bonus_rate: " << std::to_string(vRate));
//       BOOST_TEST_MESSAGE(
//               "===5=======" << "rate " << std::to_string(vRate) << "  " << "add p " << std::to_string(addu / 10000))
//               << "     old " << std::to_string((int64_t) oldu.get_amount() / 10000) << " new "
//               << std::to_string((int64_t) get_balance(pu).get_amount() / 10000);
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add 2 " << std::to_string(add2 / 10000))
//               << "      old " << std::to_string((int64_t) old2.get_amount() / 10000) << " new "
//               << std::to_string((int64_t) get_balance(w2).get_amount() / 10000);
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add 4 " << std::to_string(add4 / 10000))
//               << "      old " << std::to_string((int64_t) old4.get_amount() / 10000) << " new "
//               << std::to_string((int64_t) get_balance(w4).get_amount() / 10000);
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add v " << std::to_string(addv / 10000))
//               << "      old " << std::to_string((int64_t) oldb.get_amount() / 10000) << " new "
//               << std::to_string((int64_t) get_balance(vp).get_amount() / 10000);
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add b " << std::to_string(addb / 10000))
//               << "      old " << std::to_string((int64_t) oldb.get_amount() / 10000) << " new "
//               << std::to_string((int64_t) get_balance(bp).get_amount() / 10000);
//
//       BOOST_TEST_MESSAGE("========= end ");


       BOOST_TEST(float_equal(addu / (addu + add2 + add4), (1.0 - vRate)), " addu:(1.0 - vRate)");
       BOOST_TEST(float_equal((add2 + add4) / (addu + add2 + add4), vRate), " voter: vRate ");
//       BOOST_TEST(float_equal(add4 / add2, 2.0), " add2 * 2 == add4 ");

    }

// TODO: Complete this test
 } FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE( voting_produces_revenue_proxy, voter_bonus_tester ) {
 try {
    init();

    const account_name w2 = N(whale2);
    const account_name w4 = N(whale4);
    const account_name pu = N(produ);
    const account_name py = N(proxy);
    const account_name vp = N(eosio.vpay);
    const account_name bp = N(eosio.bpay);

    base_tester::push_action(config::system_account_name, N(regproxy), py, mvo()
            ("proxy", py)
            ("isproxy", true)
    );
    std::vector<name> pros;
    base_tester::push_action(config::system_account_name, N(voteproducer), w4, mvo()
            ("voter", w4)
            ("proxy", py)
            ("producers", pros)
    );

    votepro(w2, {pu});
    votepro(py, {pu});

    BOOST_REQUIRE_THROW(claim_bonus(w2), eosio_assert_message_exception);

    for (float vRate = 0.1; vRate <= 1.0; vRate += 0.1) {
       setglobal("to_voter_bonus_rate", std::to_string(uint32_t(vRate * 1e6)));
       produce_blocks(1);

       auto oldu = get_balance(pu);
       auto old2 = get_balance(w2);
       auto old4 = get_balance(w4);
       auto oldv = get_balance(vp);
       auto oldb = get_balance(bp);

       produce_block(fc::days(1));

       for (auto &p : producer_candidates) {
          claim_rewards(p);
       }

       claim_bonus(N(b1));
       claim_bonus(N(whale3));
       claim_bonus(w2);
       claim_bonus(w4);

//       BOOST_TEST_MESSAGE("===4===vpay==" << get_balance(vp) );
//       BOOST_TEST_MESSAGE("======bpay==" << get_balance(bp) );
//       BOOST_TEST_MESSAGE("======w2==" << get_balance(w2) );
//       BOOST_TEST_MESSAGE("======w4==" << get_balance(w4) );

       auto addu = float(get_balance(pu).get_amount() - oldu.get_amount());
       auto add2 = float(get_balance(w2).get_amount() - old2.get_amount());
       auto add4 = float(get_balance(w4).get_amount() - old4.get_amount());
       auto addb = float(get_balance(bp).get_amount() - oldb.get_amount());
       auto addv = float(get_balance(vp).get_amount() - oldv.get_amount());
//
//       BOOST_TEST_MESSAGE("==== to_voter_bonus_rate: " << std::to_string(vRate));
//       BOOST_TEST_MESSAGE(
//               "===5=======" << "rate " << std::to_string(vRate) << "  " << "add p "
//                             << std::to_string(addu / 10000));
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add 2 "
//                            << std::to_string(add2 / 10000));
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add 4 "
//                            << std::to_string(add4 / 10000));
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add v "
//                            << std::to_string(addv / 10000));
//       BOOST_TEST_MESSAGE(
//               "==========" << "rate " << std::to_string(vRate) << "  " << "add b "
//                            << std::to_string(addb / 10000));
//
//       BOOST_TEST_MESSAGE("========= end ");


       BOOST_TEST(float_equal(addu / (addu + add2 + add4), (1.0 - vRate)), " addu:(1.0 - vRate)");
       BOOST_TEST(float_equal((add2 + add4) / (addu + add2 + add4), vRate), " voter: vRate ");
//       BOOST_TEST(float_equal(add4 / add2, 2.0), " add2 * 2 == add4 ");
    }
 } FC_LOG_AND_RETHROW()
}


BOOST_FIXTURE_TEST_CASE( voting_produces_revenue_interval, voter_bonus_tester ) {
 try {
    init();

    const account_name w2 = N(whale2);
    const account_name w4 = N(whale4);
    const account_name pu = N(produ);
    const account_name vp = N(eosio.vpay);
    const account_name bp = N(eosio.bpay);

    votepro(w2, {pu});
    votepro(w4, {pu});
    setglobal("to_voter_bonus_rate", std::to_string(uint32_t(0.5 * 1e6)));
    produce_blocks(1);

    claim_rewards(pu);
    BOOST_REQUIRE_THROW(claim_bonus(w2), eosio_assert_message_exception);

    produce_block(fc::hours(23));
    BOOST_REQUIRE_THROW(claim_rewards(pu), eosio_assert_message_exception);
    BOOST_REQUIRE_THROW(claim_bonus(w2), eosio_assert_message_exception);
    produce_block(fc::hours(1));
    claim_rewards(pu);
    claim_bonus(w2);

// TODO: Complete this test
 } FC_LOG_AND_RETHROW()
}


BOOST_AUTO_TEST_SUITE_END()

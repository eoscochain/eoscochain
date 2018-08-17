#pragma once

#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace eosio {

using namespace appbase;

class cochain_relay_plugin : public appbase::plugin<cochain_relay_plugin> {
public:
    APPBASE_PLUGIN_REQUIRES((chain_plugin))

    cochain_relay_plugin();
    virtual ~cochain_relay_plugin();

    virtual void set_program_options(options_description&, options_description& cfg) override;

    void plugin_initialize(const variables_map& options);
    void plugin_startup();
    void plugin_shutdown();

private:
    std::unique_ptr<class relay> relay_;
};

}

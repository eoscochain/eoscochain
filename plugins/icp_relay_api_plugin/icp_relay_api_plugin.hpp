#pragma once

#include <icp_relay_plugin.hpp>
#include <eosio/http_plugin/http_plugin.hpp>

#include <appbase/application.hpp>
#include <eosio/chain/controller.hpp>

namespace eosio {

using std::unique_ptr;
using namespace appbase;

class icp_relay_api_plugin : public plugin<icp_relay_api_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((icp_relay_plugin)(http_plugin))

   icp_relay_api_plugin();
   virtual ~icp_relay_api_plugin();

   virtual void set_program_options(options_description&, options_description&) override;

   void plugin_initialize(const variables_map&);
   void plugin_startup();
   void plugin_shutdown();

private:
};

}

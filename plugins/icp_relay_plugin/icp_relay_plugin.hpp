#pragma once

#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include "api.hpp"

namespace icp {
   class relay; // forward declaration
}

namespace eosio {

using namespace appbase;

class icp_relay_plugin : public appbase::plugin<icp_relay_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((chain_plugin))

   icp_relay_plugin();
   virtual ~icp_relay_plugin();

   virtual void set_program_options(options_description&, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

   icp::read_only get_read_only_api() const { return icp::read_only{relay_}; }
   icp::read_write get_read_write_api() const { return icp::read_write{relay_}; }

private:
   std::shared_ptr<class icp::relay> relay_;
};

}

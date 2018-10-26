#include "icp_relay_api_plugin.hpp"

#include <eosio/chain/exceptions.hpp>

#include <fc/io/json.hpp>

namespace eosio {

static appbase::abstract_plugin& _icp_relay_api_plugin = app().register_plugin<icp_relay_api_plugin>();

icp_relay_api_plugin::icp_relay_api_plugin() {}
icp_relay_api_plugin::~icp_relay_api_plugin() {}

void icp_relay_api_plugin::set_program_options(options_description&, options_description&) {}

void icp_relay_api_plugin::plugin_initialize(const variables_map&) {}

void icp_relay_api_plugin::plugin_shutdown() {}

struct async_result_visitor : public fc::visitor<std::string> {
   template<typename T>
   std::string operator()(const T& v) const {
      return fc::json::to_string(v);
   }
};

#define CALL(api_name, api_handle, api_namespace, call_name, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
          try { \
             if (body.empty()) body = "{}"; \
             auto result = api_handle.call_name(fc::json::from_string(body).as<api_namespace::call_name ## _params>()); \
             cb(http_response_code, fc::json::to_string(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

#define CALL_ASYNC(api_name, api_handle, api_namespace, call_name, call_result, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
      if (body.empty()) body = "{}"; \
      api_handle.call_name(fc::json::from_string(body).as<api_namespace::call_name ## _params>(),\
         [cb, body](const fc::static_variant<fc::exception_ptr, call_result>& result){\
            if (result.contains<fc::exception_ptr>()) {\
               try {\
                  result.get<fc::exception_ptr>()->dynamic_rethrow_exception();\
               } catch (...) {\
                  http_plugin::handle_exception(#api_name, #call_name, body, cb);\
               }\
            } else {\
               cb(http_response_code, result.visit(async_result_visitor()));\
            }\
         });\
   }\
}

#define ICP_RELAY_RO_CALL(call_name, http_response_code) CALL(icp, ro_api, icp::read_only, call_name, http_response_code)
#define ICP_RELAY_RW_CALL(call_name, http_response_code) CALL(icp, rw_api, icp::read_write, call_name, http_response_code)
#define ICP_RELAY_RW_CALL_ASYNC(call_name, call_result, http_response_code) CALL_ASYNC(icp, rw_api, icp::read_write, call_name, call_result, http_response_code)

void icp_relay_api_plugin::plugin_startup() {
   ilog( "starting icp_relay_api_plugin" );
   auto ro_api = app().get_plugin<icp_relay_plugin>().get_read_only_api();
   auto rw_api = app().get_plugin<icp_relay_plugin>().get_read_write_api();

   auto& _http_plugin = app().get_plugin<http_plugin>();

   _http_plugin.add_api({
      ICP_RELAY_RO_CALL(get_info, 200),
      ICP_RELAY_RW_CALL(open_channel, 200)
   });
}

}

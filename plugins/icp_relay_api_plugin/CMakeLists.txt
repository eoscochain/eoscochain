file( GLOB HEADERS "*.hpp" )
add_library( icp_relay_api_plugin icp_relay_api_plugin.cpp ${HEADERS} )

target_link_libraries( icp_relay_api_plugin http_plugin chain_plugin appbase )
target_include_directories( icp_relay_api_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../icp_relay_plugin" )

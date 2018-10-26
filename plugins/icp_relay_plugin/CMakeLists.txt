file( GLOB HEADERS "*.hpp" )
add_library( icp_relay_plugin
        icp_relay_plugin.cpp icp_relay.cpp session.cpp api.cpp
        ${HEADERS} )

target_link_libraries( icp_relay_plugin chain_plugin eosio_chain appbase )
target_include_directories( icp_relay_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" )

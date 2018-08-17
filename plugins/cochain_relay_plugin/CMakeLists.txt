file( GLOB HEADERS "*.hpp" )
add_library( cochain_relay_plugin
        cochain_relay_plugin.cpp relay.cpp client.cpp connection.cpp log.cpp
        ${HEADERS} )

target_link_libraries( cochain_relay_plugin chain_plugin appbase )
target_include_directories( cochain_relay_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

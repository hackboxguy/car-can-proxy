# Plugin ABI: header-only, no dependency on the contract.
add_library(canproxy_plugin_abi INTERFACE)
add_library(canproxy::plugin_abi ALIAS canproxy_plugin_abi)
target_include_directories(canproxy_plugin_abi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
install(FILES ${CMAKE_SOURCE_DIR}/include/canproxy/plugin.h DESTINATION include/canproxy)

# Daemon core as a static library so tests link the same code.
add_library(canproxy_core STATIC
    Log.cpp Config.cpp CanSocket.cpp Mapping.cpp Publisher.cpp PluginHost.cpp)
target_include_directories(canproxy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(canproxy_core PUBLIC canproxy::contract canproxy::plugin_abi Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(canproxy_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(canproxy_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(canproxy_core PRIVATE
    CANPROXY_DEFAULT_PLUGIN_DIR="${CMAKE_INSTALL_PREFIX}/lib/can-proxy/plugins")

add_executable(can-proxyd main.cpp)
target_link_libraries(can-proxyd PRIVATE canproxy_core)
set_target_properties(can-proxyd PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(can-proxyd PRIVATE -Wall -Wextra -Werror)
install(TARGETS can-proxyd RUNTIME DESTINATION bin)

if(CANPROXY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

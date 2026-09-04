function(canproxy_core_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE canproxy_core)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

canproxy_core_test(mapping_tests)
canproxy_core_test(publisher_tests)
canproxy_core_test(config_tests)

# Stub plugins for PluginHost: one good, one with the wrong ABI, one without
# an entry symbol.
add_library(stub_plugin MODULE stub_plugin.cpp)
target_link_libraries(stub_plugin PRIVATE canproxy::plugin_abi)
set_target_properties(stub_plugin PROPERTIES PREFIX "" CXX_STANDARD 17)
add_library(stub_plugin_badabi MODULE stub_plugin.cpp)
target_link_libraries(stub_plugin_badabi PRIVATE canproxy::plugin_abi)
target_compile_definitions(stub_plugin_badabi PRIVATE STUB_BAD_ABI=1)
set_target_properties(stub_plugin_badabi PROPERTIES PREFIX "" CXX_STANDARD 17)
add_library(stub_plugin_noentry MODULE stub_plugin.cpp)
target_link_libraries(stub_plugin_noentry PRIVATE canproxy::plugin_abi)
target_compile_definitions(stub_plugin_noentry PRIVATE STUB_NO_ENTRY=1)
set_target_properties(stub_plugin_noentry PROPERTIES PREFIX "" CXX_STANDARD 17)

canproxy_core_test(plugin_host_tests)
add_dependencies(plugin_host_tests stub_plugin stub_plugin_badabi stub_plugin_noentry)
set_tests_properties(plugin_host_tests PROPERTIES
    ENVIRONMENT "STUB_DIR=${CMAKE_CURRENT_BINARY_DIR}")

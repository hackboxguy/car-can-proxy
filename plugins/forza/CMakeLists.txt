add_library(forza MODULE forza.cpp)
target_link_libraries(forza PRIVATE canproxy::plugin_abi Threads::Threads)
set_target_properties(forza PROPERTIES PREFIX "" CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CANPROXY_PLUGIN_OUTPUT_DIR})
target_compile_options(forza PRIVATE -Wall -Wextra -Werror)
install(TARGETS forza LIBRARY DESTINATION lib/can-proxy/plugins)

if(CANPROXY_BUILD_TESTS)
    add_executable(forza_parse_tests tests/forza_parse_tests.cpp)
    set_target_properties(forza_parse_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(forza_parse_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME forza_parse_tests COMMAND forza_parse_tests)
endif()

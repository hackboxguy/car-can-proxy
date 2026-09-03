# The contract is a header-only interface library. Consumers link against
# `canproxy::contract` to get the include path; nothing is compiled.
add_library(canproxy_contract INTERFACE)
add_library(canproxy::contract ALIAS canproxy_contract)
target_include_directories(canproxy_contract INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/canproxy>)

install(FILES can_proxy_contract.h DESTINATION include/canproxy)

if(CANPROXY_BUILD_TESTS)
    add_executable(contract_tests tests/contract_tests.c)
    target_link_libraries(contract_tests PRIVATE canproxy::contract)
    set_target_properties(contract_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(contract_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
    add_test(NAME contract_tests COMMAND contract_tests)

    add_executable(contract_cxx_tests tests/contract_cxx_tests.cpp)
    target_link_libraries(contract_cxx_tests PRIVATE canproxy::contract)
    set_target_properties(contract_cxx_tests PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_options(contract_cxx_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
    add_test(NAME contract_cxx_tests COMMAND contract_cxx_tests)
endif()

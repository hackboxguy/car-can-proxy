add_library(sim MODULE sim.cpp)
target_link_libraries(sim PRIVATE canproxy::plugin_abi Threads::Threads)
set_target_properties(sim PROPERTIES PREFIX "" CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CANPROXY_PLUGIN_OUTPUT_DIR})
target_compile_options(sim PRIVATE -Wall -Wextra -Werror)
install(TARGETS sim LIBRARY DESTINATION lib/can-proxy/plugins)

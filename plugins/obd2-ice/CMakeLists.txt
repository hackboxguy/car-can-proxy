add_library(obd2-ice MODULE obd2_ice.cpp)
target_link_libraries(obd2-ice PRIVATE canproxy::plugin_abi canproxy::obd Threads::Threads)
set_target_properties(obd2-ice PROPERTIES PREFIX "" CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CANPROXY_PLUGIN_OUTPUT_DIR})
target_compile_options(obd2-ice PRIVATE -Wall -Wextra -Werror)
install(TARGETS obd2-ice LIBRARY DESTINATION lib/can-proxy/plugins)

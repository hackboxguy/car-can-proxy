cmake_minimum_required(VERSION 3.16)
project(car-can-proxy VERSION 0.1.0 LANGUAGES C CXX)

# A proxy service offering one unified CAN frame contract to instrument
# cluster applications, independent of the vehicle's own CAN messaging.
#
#   contract/   the frame contract (header-only) and its tests
#   include/    the plugin ABI (canproxy/plugin.h)
#   core/       can-proxyd daemon
#   plugins/    vehicle plugins (.so): sim, later obd2-ice, emu-ev, emu-hybrid
#   tools/      contract-dump
#   libobd/     J1979 / UDS / ISO-TP client library (bucket 4+)

option(CANPROXY_BUILD_TESTS "Build unit tests" ON)

if(CANPROXY_BUILD_TESTS)
    enable_testing()
endif()

find_package(Threads REQUIRED)

add_subdirectory(contract)
add_subdirectory(libobd)
add_subdirectory(core)
add_subdirectory(plugins)
add_subdirectory(tools)
if(CANPROXY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

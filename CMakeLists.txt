cmake_minimum_required(VERSION 3.16)
project(car-can-proxy VERSION 0.1.0 LANGUAGES C CXX)

# A proxy service offering one unified CAN frame contract to instrument
# cluster applications, independent of the vehicle's own CAN messaging.
#
#   contract/   the frame contract (header-only) and its tests   [bucket 1]
#   core/       can-proxyd daemon                                  [bucket 2]
#   plugins/    vehicle plugins (.so)                              [bucket 2+]
#   libobd/     J1979 / UDS / ISO-TP client library                [bucket 4+]

option(CANPROXY_BUILD_TESTS "Build unit tests" ON)

if(CANPROXY_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(contract)

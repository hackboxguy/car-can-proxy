# Integration tests need a vcan interface; they report SKIP (77) without one.
add_test(NAME sim_cycle_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/sim_cycle_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(sim_cycle_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

set(CANPROXY_EMULATOR "${CMAKE_SOURCE_DIR}/../car-can-emulator/build/car-can-emulator"
    CACHE FILEPATH "car-can-emulator binary for the obd2-ice integration test")
add_test(NAME obd2_ice_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/obd2_ice_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(obd2_ice_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
    ENVIRONMENT "CANPROXY_EMULATOR=${CANPROXY_EMULATOR}")

add_test(NAME emu_ev_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/emu_ev_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(emu_ev_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
    ENVIRONMENT "CANPROXY_EMULATOR=${CANPROXY_EMULATOR}")

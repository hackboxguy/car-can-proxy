# Integration tests carry the label "integration": they need vcan interfaces
# and own them while running, so scripts/deploy.sh (and the image hook, whose
# chroot shares the host's network namespace) run `ctest -LE integration`.
# Integration tests need a vcan interface; they report SKIP (77) without one.
add_test(NAME sim_cycle_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/sim_cycle_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(sim_cycle_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS integration)

set(CANPROXY_EMULATOR "${CMAKE_SOURCE_DIR}/../car-can-emulator/build/car-can-emulator"
    CACHE FILEPATH "car-can-emulator binary for the obd2-ice integration test")
add_test(NAME obd2_ice_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/obd2_ice_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(obd2_ice_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS integration
    ENVIRONMENT "CANPROXY_EMULATOR=${CANPROXY_EMULATOR}")

add_test(NAME emu_ev_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/emu_ev_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(emu_ev_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS integration
    ENVIRONMENT "CANPROXY_EMULATOR=${CANPROXY_EMULATOR}")

add_test(NAME replay_fixture_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/replay_fixture_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(replay_fixture_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS integration)

add_test(NAME forza_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/forza_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(forza_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS integration)

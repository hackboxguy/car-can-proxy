# Integration tests need a vcan interface; they report SKIP (77) without one.
add_test(NAME sim_cycle_test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/integration/sim_cycle_test.sh ${CMAKE_BINARY_DIR})
set_tests_properties(sim_cycle_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

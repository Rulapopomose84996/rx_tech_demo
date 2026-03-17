add_executable(rxtech_integration_fake_tests
    test_bench_fake.cpp
)

target_link_libraries(rxtech_integration_fake_tests
    PRIVATE
        rxtech_benchmark_core
        rxtech_rx_only_mode
)

add_test(NAME rxtech_integration_fake_tests COMMAND rxtech_integration_fake_tests)
set_tests_properties(rxtech_integration_fake_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(rxtech_integration_fake_tests)

if(UNIX AND NOT APPLE)
    add_executable(rxtech_integration_tests
        test_bench_smoke.cpp
    )

    target_link_libraries(rxtech_integration_tests
        PRIVATE
            rxtech_benchmark_core
            rxtech_socket_backend
            rxtech_rx_only_mode
    )

    add_test(NAME rxtech_integration_tests COMMAND rxtech_integration_tests)
    set_tests_properties(rxtech_integration_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    rxtech_apply_warnings(rxtech_integration_tests)
endif()

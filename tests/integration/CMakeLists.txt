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
            rxtech_parse_mode
    )

    add_test(NAME rxtech_integration_tests COMMAND rxtech_integration_tests)
    set_tests_properties(rxtech_integration_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    rxtech_apply_warnings(rxtech_integration_tests)

    add_executable(test_socket_three_port
        test_socket_three_port.cpp
    )

    target_link_libraries(test_socket_three_port
        PRIVATE
            rxtech_benchmark_core
            rxtech_socket_backend
            rxtech_parse_mode
    )

    add_test(NAME test_socket_three_port COMMAND test_socket_three_port)
    set_tests_properties(test_socket_three_port PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    rxtech_apply_warnings(test_socket_three_port)
endif()

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
rxtech_apply_warnings(rxtech_integration_tests)

add_library(rxtech_app_common
    common/cli_args.cpp
    common/app_main_common.cpp
)

target_include_directories(rxtech_app_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(rxtech_app_common
    PUBLIC
        rxtech_benchmark_core
        rxtech_rx_only_mode
        rxtech_parse_mode
        rxtech_spsc_mode
)

if(TARGET rxtech_socket_backend)
    target_link_libraries(rxtech_app_common PUBLIC rxtech_socket_backend)
    target_compile_definitions(rxtech_app_common PUBLIC RXTECH_HAS_SOCKET_BACKEND=1)
endif()

if(TARGET rxtech_xdp_backend)
    target_link_libraries(rxtech_app_common PUBLIC rxtech_xdp_backend)
    target_compile_definitions(rxtech_app_common PUBLIC RXTECH_HAS_AF_XDP_BACKEND=1)
endif()

if(TARGET rxtech_dpdk_backend)
    target_link_libraries(rxtech_app_common PUBLIC rxtech_dpdk_backend)
    target_compile_definitions(rxtech_app_common PUBLIC RXTECH_HAS_DPDK_BACKEND=1)
endif()

rxtech_apply_warnings(rxtech_app_common)

add_executable(rxbench_socket rxbench_socket_main.cpp)
target_link_libraries(rxbench_socket PRIVATE rxtech_app_common)
rxtech_apply_warnings(rxbench_socket)

add_executable(rxbench_xdp rxbench_xdp_main.cpp)
target_link_libraries(rxbench_xdp PRIVATE rxtech_app_common)
rxtech_apply_warnings(rxbench_xdp)

add_executable(rxbench_dpdk rxbench_dpdk_main.cpp)
target_link_libraries(rxbench_dpdk PRIVATE rxtech_app_common)
rxtech_apply_warnings(rxbench_dpdk)

add_library(rx_receiver_core
    protocol/src/parser.cpp
    storage/src/reassembly.cpp
    sidecar/src/metrics.cpp
    runtime/src/rx_config.cpp
    runtime/src/receive_runner.cpp
)

target_include_directories(rx_receiver_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/storage
        ${CMAKE_CURRENT_SOURCE_DIR}/sidecar
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
)

target_compile_features(rx_receiver_core PUBLIC cxx_std_17)
rxtech_apply_warnings(rx_receiver_core)

add_library(rx_receiver_dpdk_ingress
    ingress/dpdk/src/dpdk_backend.cpp
)

target_include_directories(rx_receiver_dpdk_ingress
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ingress/dpdk
)

target_link_libraries(rx_receiver_dpdk_ingress
    PUBLIC
        rx_receiver_core
)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBDPDK QUIET libdpdk)
    endif()

    if(LIBDPDK_FOUND)
        target_include_directories(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_INCLUDE_DIRS})
        target_link_libraries(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_LIBRARIES})
        target_compile_options(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_CFLAGS_OTHER})
        target_compile_definitions(rx_receiver_dpdk_ingress PRIVATE RXTECH_HAS_DPDK_RUNTIME=1)
        set_target_properties(rx_receiver_dpdk_ingress PROPERTIES CXX_EXTENSIONS ON)
    endif()
endif()

rxtech_apply_warnings(rx_receiver_dpdk_ingress)

add_library(rx_receiver_app_common
    app/cli/cli_args.cpp
    app/common/app_main_common.cpp
)

target_include_directories(rx_receiver_app_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/app/cli
        ${CMAKE_CURRENT_SOURCE_DIR}/app/common
)

target_link_libraries(rx_receiver_app_common
    PUBLIC
        rx_receiver_core
        rx_receiver_dpdk_ingress
)

target_compile_definitions(rx_receiver_app_common PUBLIC RXTECH_HAS_DPDK_BACKEND=1)
rxtech_apply_warnings(rx_receiver_app_common)

add_executable(rx_receiver_dpdk app/main_dpdk.cpp)
target_link_libraries(rx_receiver_dpdk PRIVATE rx_receiver_app_common)
rxtech_apply_warnings(rx_receiver_dpdk)

add_executable(rxbench_dpdk app/main_dpdk.cpp)
target_link_libraries(rxbench_dpdk PRIVATE rx_receiver_app_common)
rxtech_apply_warnings(rxbench_dpdk)

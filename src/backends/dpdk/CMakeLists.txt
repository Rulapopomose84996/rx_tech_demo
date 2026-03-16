add_library(rxtech_dpdk_backend
    src/dpdk_backend.cpp
)

target_include_directories(rxtech_dpdk_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rxtech_dpdk_backend
    PUBLIC
        rxtech_benchmark_core
)

rxtech_apply_warnings(rxtech_dpdk_backend)

add_library(rxtech_rx_only_mode
    src/rx_only_mode.cpp
)

target_include_directories(rxtech_rx_only_mode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rxtech_rx_only_mode
    PUBLIC
        rxtech_benchmark_core
)

rxtech_apply_warnings(rxtech_rx_only_mode)

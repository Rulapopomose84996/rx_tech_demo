add_library(rxtech_receiver_core
    src/rx_config.cpp
    src/parser.cpp
    src/reassembly.cpp
    src/metrics.cpp
    src/receive_runner.cpp
)

target_include_directories(rxtech_receiver_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rxtech_receiver_core PUBLIC cxx_std_17)
rxtech_apply_warnings(rxtech_receiver_core)

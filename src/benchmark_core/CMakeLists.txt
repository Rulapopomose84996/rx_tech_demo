add_library(rxtech_benchmark_core
    src/rx_config.cpp
    src/scenario.cpp
    src/parser.cpp
    src/metrics.cpp
    src/report_writer_json.cpp
    src/report_writer_csv.cpp
    src/bench_context.cpp
    src/bench_runner.cpp
)

target_include_directories(rxtech_benchmark_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rxtech_benchmark_core PUBLIC cxx_std_17)
rxtech_apply_warnings(rxtech_benchmark_core)

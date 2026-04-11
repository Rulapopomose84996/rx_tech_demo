find_package(benchmark QUIET)

if(NOT TARGET benchmark::benchmark)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(google_benchmark_fetch
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.4.tar.gz
    )
    FetchContent_MakeAvailable(google_benchmark_fetch)
endif()

add_executable(rxtech_benchmark_core benchmark_core.cpp)
target_link_libraries(rxtech_benchmark_core PRIVATE rx_receiver_core benchmark::benchmark benchmark::benchmark_main)
rxtech_apply_warnings(rxtech_benchmark_core)

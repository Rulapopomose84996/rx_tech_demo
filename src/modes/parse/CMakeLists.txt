add_library(rxtech_parse_mode
    src/parse_mode.cpp
)

target_include_directories(rxtech_parse_mode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rxtech_parse_mode
    PUBLIC
        rxtech_benchmark_core
)

rxtech_apply_warnings(rxtech_parse_mode)

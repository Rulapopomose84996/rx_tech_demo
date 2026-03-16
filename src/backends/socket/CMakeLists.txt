add_library(rxtech_socket_backend
    src/socket_backend.cpp
)

target_include_directories(rxtech_socket_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rxtech_socket_backend
    PUBLIC
        rxtech_benchmark_core
)

rxtech_apply_warnings(rxtech_socket_backend)

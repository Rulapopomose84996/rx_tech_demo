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

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBDPDK QUIET libdpdk)
    endif()

    if(LIBDPDK_FOUND)
        target_include_directories(rxtech_dpdk_backend PRIVATE ${LIBDPDK_INCLUDE_DIRS})
        target_link_libraries(rxtech_dpdk_backend PRIVATE ${LIBDPDK_LIBRARIES})
        target_compile_options(rxtech_dpdk_backend PRIVATE ${LIBDPDK_CFLAGS_OTHER})
        target_compile_definitions(rxtech_dpdk_backend PRIVATE RXTECH_HAS_DPDK_RUNTIME=1)
        set_target_properties(rxtech_dpdk_backend PROPERTIES CXX_EXTENSIONS ON)
    endif()
endif()

rxtech_apply_warnings(rxtech_dpdk_backend)

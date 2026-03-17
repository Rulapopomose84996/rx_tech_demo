if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBBPF QUIET libbpf)
    endif()

    if(LIBBPF_FOUND)
        add_library(rxtech_xdp_backend
            src/xdp_backend.cpp
        )

        target_include_directories(rxtech_xdp_backend
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
            PRIVATE
                ${LIBBPF_INCLUDE_DIRS}
        )

        target_link_libraries(rxtech_xdp_backend
            PUBLIC
                rxtech_benchmark_core
            PRIVATE
                ${LIBBPF_LIBRARIES}
        )

        set_target_properties(rxtech_xdp_backend PROPERTIES
            CXX_EXTENSIONS ON
        )
        target_compile_options(rxtech_xdp_backend PRIVATE ${LIBBPF_CFLAGS_OTHER})
        rxtech_apply_warnings(rxtech_xdp_backend)
    else()
        message(WARNING "libbpf not found, AF_XDP backend target is disabled")
    endif()
else()
    add_library(rxtech_xdp_backend
        src/xdp_backend.cpp
    )

    target_include_directories(rxtech_xdp_backend
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(rxtech_xdp_backend
        PUBLIC
            rxtech_benchmark_core
    )

    rxtech_apply_warnings(rxtech_xdp_backend)
endif()

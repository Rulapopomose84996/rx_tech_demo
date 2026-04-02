if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBBPF QUIET libbpf)
    endif()

    if(LIBBPF_FOUND)
        add_library(rx_legacy_af_xdp
            src/xdp_backend.cpp
        )

        target_include_directories(rx_legacy_af_xdp
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
            PRIVATE
                ${LIBBPF_INCLUDE_DIRS}
        )

        target_link_libraries(rx_legacy_af_xdp
            PUBLIC
                rx_receiver_core
            PRIVATE
                ${LIBBPF_LIBRARIES}
        )

        set_target_properties(rx_legacy_af_xdp PROPERTIES
            CXX_EXTENSIONS ON
        )
        target_compile_options(rx_legacy_af_xdp PRIVATE ${LIBBPF_CFLAGS_OTHER})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(rx_legacy_af_xdp PRIVATE -Wno-deprecated-declarations)
        endif()
        rxtech_apply_warnings(rx_legacy_af_xdp)
    else()
        message(WARNING "libbpf not found, AF_XDP backend target is disabled")
    endif()
else()
    add_library(rx_legacy_af_xdp
        src/xdp_backend.cpp
    )

    target_include_directories(rx_legacy_af_xdp
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(rx_legacy_af_xdp
        PUBLIC
            rx_receiver_core
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rx_legacy_af_xdp PRIVATE -Wno-deprecated-declarations)
    endif()
    rxtech_apply_warnings(rx_legacy_af_xdp)
endif()

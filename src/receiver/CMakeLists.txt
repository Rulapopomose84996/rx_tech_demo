add_library(rx_receiver_core
    admit/cpi_admission.cpp
    core/cpi_state_coordinator.cpp
    core/owner_loop.cpp
    core/owner_loop_summary.cpp
    finalize/cpi_finalizer.cpp
    output/cpi_consumer.cpp
    protocol/data_order_tracker.cpp
    protocol/packet_pipeline.cpp
    protocol/protocol_sequence_interpreter.cpp
    protocol/sample_packet_parser.cpp
    protocol/sample_packet_validator.cpp
    protocol/udp_payload_assembler.cpp
    sidecar/metrics.cpp
    sidecar/runtime_status_reporter.cpp
    sidecar/status_panel.cpp
    storage/progress_tracker.cpp
    storage/slot_writer.cpp
    runtime/rx_config.cpp
    runtime/raw_frame_recorder.cpp
    runtime/receive_runner.cpp
    verify/cpi_verifier.cpp
)

target_include_directories(rx_receiver_core
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/admit
    ${CMAKE_CURRENT_SOURCE_DIR}/finalize
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/sidecar
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/output
    ${CMAKE_CURRENT_SOURCE_DIR}/core/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/sidecar/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/verify
)

target_compile_features(rx_receiver_core PUBLIC cxx_std_17)

if(RXTECH_ENABLE_DEBUG_DIAGNOSTICS)
    target_compile_definitions(rx_receiver_core PUBLIC RXTECH_DEBUG_DIAGNOSTICS=1)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(rx_receiver_core PUBLIC pthread)
endif()

rxtech_apply_warnings(rx_receiver_core)

add_library(rx_receiver_dpdk_ingress
    ingress/dpdk/arp_responder.cpp
    ingress/dpdk/dpdk_backend.cpp
)

target_include_directories(rx_receiver_dpdk_ingress
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ingress/dpdk/internal
)

target_link_libraries(rx_receiver_dpdk_ingress
    PUBLIC
    rx_receiver_core
)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(LIBDPDK QUIET libdpdk)
    endif()

    if(LIBDPDK_FOUND)
        target_include_directories(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_INCLUDE_DIRS})
        target_link_libraries(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_LIBRARIES})
        target_compile_options(rx_receiver_dpdk_ingress PRIVATE ${LIBDPDK_CFLAGS_OTHER})
        target_compile_definitions(rx_receiver_dpdk_ingress PRIVATE RXTECH_HAS_DPDK_RUNTIME=1)
        set_target_properties(rx_receiver_dpdk_ingress PROPERTIES CXX_EXTENSIONS ON)
    endif()
endif()

rxtech_apply_warnings(rx_receiver_dpdk_ingress)

add_library(rx_receiver_app
    app/cli_args.cpp
    app/run_app.cpp
)

target_include_directories(rx_receiver_app
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ingress/dpdk/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/internal
)

target_link_libraries(rx_receiver_app
    PUBLIC
    rx_receiver_core
    rx_receiver_dpdk_ingress
)

rxtech_apply_warnings(rx_receiver_app)

add_executable(rx_receiver_dpdk app/main_dpdk.cpp)
target_link_libraries(rx_receiver_dpdk PRIVATE rx_receiver_app)
rxtech_apply_warnings(rx_receiver_dpdk)

add_library(rx_slow_consumer_harness
    output/slow_consumer.cpp
)

target_link_libraries(rx_slow_consumer_harness
    PUBLIC rx_receiver_core
)

rxtech_apply_warnings(rx_slow_consumer_harness)

if(BUILD_REPLAY AND UNIX AND NOT CMAKE_CROSSCOMPILING)
    add_library(rx_receiver_filereplay
        ingress/filereplay/file_replay_backend.cpp
    )
    target_include_directories(rx_receiver_filereplay
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ingress/filereplay
        ${CMAKE_SOURCE_DIR}/src/replay_sender
    )
    target_link_libraries(rx_receiver_filereplay
        PUBLIC rx_receiver_core
        PRIVATE nlohmann_json replay_sender_lib
    )
    target_compile_features(rx_receiver_filereplay PUBLIC cxx_std_17)

    if(UNIX AND NOT APPLE)
        target_link_libraries(rx_receiver_filereplay PUBLIC pthread)
    endif()

    rxtech_apply_warnings(rx_receiver_filereplay)
endif()

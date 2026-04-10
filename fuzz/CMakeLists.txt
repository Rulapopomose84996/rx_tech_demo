if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BUILD_FUZZERS=ON requires clang/clang++ with libFuzzer support")
endif()

set(_rxtech_fuzz_flags -fsanitize=fuzzer,address -fno-omit-frame-pointer)

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
    OUTPUT_VARIABLE _rxtech_clang_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-target-triple
    OUTPUT_VARIABLE _rxtech_clang_target_triple
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT _rxtech_clang_target_triple)
    set(_rxtech_clang_target_triple "${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu")
endif()

set(_rxtech_clang_builtins
    "${_rxtech_clang_resource_dir}/lib/${_rxtech_clang_target_triple}/libclang_rt.builtins.a")

function(rxtech_link_clang_builtins target_name)
    if(EXISTS "${_rxtech_clang_builtins}")
        target_link_libraries(${target_name} PRIVATE "${_rxtech_clang_builtins}")
    endif()
endfunction()

add_executable(fuzz_sample_packet_parser fuzz_sample_packet_parser.cpp)
target_link_libraries(fuzz_sample_packet_parser PRIVATE rx_receiver_core atomic)
target_compile_options(fuzz_sample_packet_parser PRIVATE ${_rxtech_fuzz_flags})
target_link_options(fuzz_sample_packet_parser PRIVATE ${_rxtech_fuzz_flags})
rxtech_link_clang_builtins(fuzz_sample_packet_parser)
rxtech_apply_warnings(fuzz_sample_packet_parser)

add_executable(fuzz_udp_payload_assembler fuzz_udp_payload_assembler.cpp)
target_link_libraries(fuzz_udp_payload_assembler PRIVATE rx_receiver_core atomic)
target_compile_options(fuzz_udp_payload_assembler PRIVATE ${_rxtech_fuzz_flags})
target_link_options(fuzz_udp_payload_assembler PRIVATE ${_rxtech_fuzz_flags})
rxtech_link_clang_builtins(fuzz_udp_payload_assembler)
rxtech_apply_warnings(fuzz_udp_payload_assembler)

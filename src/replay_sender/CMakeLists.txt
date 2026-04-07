# src/replay_sender/CMakeLists.txt
# Linux-only AF_PACKET replay sender executable + shared helper library.
# This file is only included from the top-level CMakeLists.txt when
# UNIX AND NOT CMAKE_CROSSCOMPILING
# so no explicit platform guard is needed here.

# ── replay_sender_lib  ────────────────────────────────────────────────────────
# Shared code used by both the standalone executable AND integration tests
# (via rx_receiver_filereplay).
add_library(replay_sender_lib
    manifest_loader.cpp
    frame_builder.cpp
    replay_config.cpp
    af_packet_sender.cpp
)

target_include_directories(replay_sender_lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(replay_sender_lib
    PRIVATE
    nlohmann_json
)

target_compile_features(replay_sender_lib PUBLIC cxx_std_17)
rxtech_apply_warnings(replay_sender_lib)

# ── rx_replay_sender  ─────────────────────────────────────────────────────────
add_executable(rx_replay_sender
    replay_main.cpp
)

target_link_libraries(rx_replay_sender
    PRIVATE
    replay_sender_lib
)

rxtech_apply_warnings(rx_replay_sender)

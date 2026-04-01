add_library(rx_replay_sender_core
    replay/src/replay_manifest.cpp
    replay/src/replay_sender.cpp
)

target_include_directories(rx_replay_sender_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/replay
)

target_compile_features(rx_replay_sender_core PUBLIC cxx_std_17)
rxtech_apply_warnings(rx_replay_sender_core)

if(UNIX AND NOT APPLE)
    add_executable(rx_replay_sender
        replay/src/main_replay_sender.cpp
    )
    target_link_libraries(rx_replay_sender PRIVATE rx_replay_sender_core)
    rxtech_apply_warnings(rx_replay_sender)
endif()

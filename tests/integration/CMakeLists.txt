add_executable(rxtech_integration_fake_tests
    test_receive_runner_fake.cpp
)

target_link_libraries(rxtech_integration_fake_tests
    PRIVATE
    rx_receiver_core
)

if(UNIX AND NOT APPLE)
    target_link_libraries(rxtech_integration_fake_tests PRIVATE pthread)
endif()

add_test(NAME rxtech_integration_fake_tests COMMAND rxtech_integration_fake_tests)
set_tests_properties(rxtech_integration_fake_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(rxtech_integration_fake_tests)

if(UNIX AND NOT CMAKE_CROSSCOMPILING)
    add_executable(rxtech_integration_file_replay_tests
        test_file_replay_pipeline.cpp
    )
    target_link_libraries(rxtech_integration_file_replay_tests
        PRIVATE rx_receiver_core rx_receiver_filereplay pthread
    )
    add_test(NAME rxtech_integration_file_replay_tests COMMAND rxtech_integration_file_replay_tests)
    set_tests_properties(rxtech_integration_file_replay_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    rxtech_apply_warnings(rxtech_integration_file_replay_tests)
endif()

add_executable(rxtech_integration_slow_consumer_tests
    test_slow_consumer_pressure.cpp
)
target_link_libraries(rxtech_integration_slow_consumer_tests
    PRIVATE rx_receiver_core
)

if(UNIX AND NOT APPLE)
    target_link_libraries(rxtech_integration_slow_consumer_tests PRIVATE pthread)
endif()

add_test(NAME rxtech_integration_slow_consumer_tests COMMAND rxtech_integration_slow_consumer_tests)
set_tests_properties(rxtech_integration_slow_consumer_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
rxtech_apply_warnings(rxtech_integration_slow_consumer_tests)

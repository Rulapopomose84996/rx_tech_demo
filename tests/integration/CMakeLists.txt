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

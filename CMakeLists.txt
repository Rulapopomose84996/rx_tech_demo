cmake_minimum_required(VERSION 3.16)

project(rx_tech_demo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cmake/options.cmake)
include(cmake/warnings.cmake)
include(cmake/dependencies.cmake)

add_subdirectory(src/receiver)
add_subdirectory(src/sender)
add_subdirectory(src/legacy)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

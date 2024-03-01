cmake_minimum_required(VERSION 3.10)
project(srclang
        VERSION 0.0.1
        DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)
add_subdirectory(bin)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_testing()
    add_subdirectory(tests)
endif ()
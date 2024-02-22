cmake_minimum_required(VERSION 3.10)
project(srclang
        VERSION 0.0.1
        DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

if (WIN32)

else ()
    find_library(READLINE readline REQUIRED)
    find_library(DL dl REQUIRED)
endif ()
add_subdirectory(src)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_testing()
    add_subdirectory(tests)
endif ()
cmake_minimum_required(VERSION 3.10)
project(srclang
        VERSION 0.0.2
        DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

find_library(DL dl REQUIRED)
find_library(FFI ffi REQUIRED)

include(GNUInstallDirs)

add_subdirectory(src)
add_subdirectory(modules)

enable_testing()
add_subdirectory(tests)
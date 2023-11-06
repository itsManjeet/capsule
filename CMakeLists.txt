cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

if (WIN32)
set(READLINE "")
else()
find_library(READLINE readline REQUIRED)

find_library(DL dl REQUIRED)
find_library(FFI ffi REQUIRED)

include(ExternalProject)
ExternalProject_Add(tinycc
        GIT_REPOSITORY git://repo.or.cz/tinycc.git
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/libtcc
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND ${MAKE_EXECUTABLE}
        INSTALL_COMMAND ${MAKE_EXECUTABLE} install)
ExternalProject_Get_Property(tinycc INSTALL_DIR)
include_directories(${INSTALL_DIR}/include)
link_directories(${INSTALL_DIR}/lib)
set(TCC libtcc.a)
endif ()

add_subdirectory(src)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
enable_testing()
add_subdirectory(tests)
endif ()
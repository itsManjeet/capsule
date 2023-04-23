cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include(GNUInstallDirs)
include(ExternalProject)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
find_library(DL dl REQUIRED)
find_library(FFI ffi REQUIRED)

add_executable(${PROJECT_NAME} src/srclang.cxx)
target_link_libraries(${PROJECT_NAME} ${FFI} ${DL} -rdynamic)
# target_compile_definitions(${PROJECT_NAME} PUBLIC -DSRCLANG_GC_DEBUG)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
enable_testing()
file(GLOB_RECURSE TestFiles "tests/*.src")
foreach(testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND tests/run_test.sh ${PROJECT_BINARY_DIR}/${PROJECT_NAME} tests/${testName}.src)
endforeach()

endif ()
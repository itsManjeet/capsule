cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

find_library(FFI ffi REQUIRED)
find_library(DL dl REQUIRED)

add_executable(${PROJECT_NAME} src/srclang.cxx)
target_link_libraries(${PROJECT_NAME} ffi dl -static -rdynamic)
# target_compile_definitions(${PROJECT_NAME} PUBLIC -DSRCLANG_GC_DEBUG)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
enable_testing()
file(GLOB_RECURSE TestFiles "tests/*.src")
foreach(testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND ./tests/run_test.sh ${testFile})
endforeach()

endif ()
cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
find_library(FFI ffi REQUIRED)
find_library(DL dl REQUIRED)

find_library(LIBTCC tcc)
find_program(MAKE_EXECUTABLE NAMES gmake make mingw32-make REQUIRED)
include(ExternalProject)
if (NOT LIBTCC)
    ExternalProject_Add(tinycc
            GIT_REPOSITORY "https://repo.or.cz/tinycc.git"
            GIT_TAG mob
            CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
            BUILD_COMMAND ${MAKE_EXECUTABLE}
            INSTALL_COMMAND ${MAKE_EXECUTABLE} install)
    ExternalProject_Get_Property(tinycc INSTALL_DIR)
    include_directories(${INSTALL_DIR}/include)
    link_directories(${INSTALL_DIR}/lib)
    set(LIBTCC tcc)
endif ()

add_executable(${PROJECT_NAME} src/srclang.cxx)
add_dependencies(${PROJECT_NAME} tinycc)

target_link_libraries(${PROJECT_NAME} ${FFI} ${DL} ${LIBTCC} -static -rdynamic)
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
cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(ExternalProject)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
find_library(DL dl REQUIRED)
find_library(FFI ffi)
find_library(TCC tcc)
find_program(MAKE_EXECUTABLE NAMES gmake make mingw32-make REQUIRED)

add_executable(${PROJECT_NAME} src/srclang.cxx)
if ((NOT FFI) OR (FFI MATCHES ".*\\.so"))
ExternalProject_Add(libffi
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/libffi
        DOWNLOAD_COMMAND cd <SOURCE_DIR> && ./autogen.sh
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND ${MAKE_EXECUTABLE}
        INSTALL_COMMAND ${MAKE_EXECUTABLE} install)
ExternalProject_Get_Property(libffi INSTALL_DIR)
include_directories(${INSTALL_DIR}/include)
link_directories(${INSTALL_DIR}/lib)
set(FFI ${INSTALL_DIR}/lib/libffi.a)
add_dependencies(${PROJECT_NAME} libffi)
endif ()

if (NOT TCC)
ExternalProject_Add(tinycc
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/libtcc
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND ${MAKE_EXECUTABLE}
        INSTALL_COMMAND ${MAKE_EXECUTABLE} install)
ExternalProject_Get_Property(tinycc INSTALL_DIR)
include_directories(${INSTALL_DIR}/include)
link_directories(${INSTALL_DIR}/lib)
set(LIBTCC ${INSTALL_DIR}/lib/libtcc.a)
add_dependencies(${PROJECT_NAME} tinycc)
endif ()


target_link_libraries(${PROJECT_NAME} ${FFI} ${DL} ${LIBTCC} -static -rdynamic)
# target_compile_definitions(${PROJECT_NAME} PUBLIC -DSRCLANG_GC_DEBUG)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
enable_testing()
file(GLOB_RECURSE TestFiles "tests/*.src")
foreach(testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.sh ${PROJECT_BINARY_DIR}/${PROJECT_NAME} ${testFile})
endforeach()

endif ()
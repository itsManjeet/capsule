cmake_minimum_required(VERSION 3.10)
project(srclang
    VERSION 0.0.1
    DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

include(GNUInstallDirs)
include(ExternalProject)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")
find_library(DL dl REQUIRED)
find_library(FFI ffi REQUIRED)
find_program(MAKE_EXECUTABLE NAMES gmake make mingw32-make REQUIRED)
ExternalProject_Add(tinycc
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/libtcc
        CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND ${MAKE_EXECUTABLE}
        INSTALL_COMMAND ${MAKE_EXECUTABLE} install)
ExternalProject_Get_Property(tinycc INSTALL_DIR)
include_directories(${INSTALL_DIR}/include)
link_directories(${INSTALL_DIR}/lib)
set(LIBTCC ${INSTALL_DIR}/lib/libtcc.a)


add_executable(${PROJECT_NAME} src/srclang.cxx
        src/Value.hxx src/Value.cxx
        src/utils.cxx src/Utilities.hxx
        src/SymbolTable.cxx src/SymbolTable.hxx
        src/DebugInfo.cxx src/DebugInfo.hxx
        src/Instructions.cxx src/Instructions.hxx
        src/ByteCode.cxx src/ByteCode.hxx
        src/Function.cxx src/Function.hxx
        src/MemoryManager.cxx src/MemoryManager.hxx
        src/Builtin.cxx src/Builtin.hxx
        src/Options.cxx src/Options.hxx
        src/Compiler.cxx src/Compiler.hxx
        src/Interpreter.cxx src/Interpreter.hxx
        src/Language.cxx src/Language.hxx
        src/ProjectManager.cxx src/ProjectManager.hxx
        )
add_dependencies(${PROJECT_NAME} tinycc)
target_link_libraries(${PROJECT_NAME} ${FFI} ${LIBTCC} ${DL} -rdynamic)
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
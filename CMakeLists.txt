cmake_minimum_required(VERSION 3.10)
project(srclang
        VERSION 0.0.2
        DESCRIPTION "Source Programming Language")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(POSITION_INDEPENDENT_CODE ON)

add_definitions(-DSRCLANG_GC_STRESS)
# add_definitions(-DSRCLANG_GC_DEBUG)

if(UNIX)
    find_library(DL dl REQUIRED)
else()
    set(DL "")
endif()

find_library(FFI ffi)
if(FFI)
    add_definitions(-DWITH_FFI)
else()
    set(FFI "")
endif()

find_library(READLINE readline)
if(READLINE)
    add_definitions(-DWITH_READLINE)
else()
    set(READLINE "")
endif()
include(GNUInstallDirs)

file(GLOB_RECURSE SourceFiles "src/*.cpp" "src/*.h")
add_executable(${PROJECT_NAME} ${SourceFiles})

set_target_properties(${PROJECT_NAME}
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        PUBLIC_HEADER "src/Value.h;src/ByteCode.h;src/DebugInfo.h;src/Instructions.h;src/Interpreter.h;src/Compiler.h;src/SymbolTable.h;src/MemoryManager.h")

target_link_options(${PROJECT_NAME} PRIVATE
        -rdynamic)
        
target_link_libraries(${PROJECT_NAME} PRIVATE
        ${FFI} ${DL} ${READLINE})

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SrcLang)

add_subdirectory(modules)

enable_testing()
add_subdirectory(tests)
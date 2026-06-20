cmake_minimum_required(VERSION 3.16)

project(srclang
  VERSION 0.1.0
  DESCRIPTION "Native assembly-oriented programming language compiler in pure C"
  LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SRCLANG_BUILD_TESTS "Build srclang tests" ON)

add_executable(srclang
  src/main.c
  src/asm_arm64.c
  src/asm_x64.c
  src/ast.c
  src/context.c
  src/module.c
  src/native_bytecode.c
  src/parser.c
  src/scanner.c
)

target_include_directories(srclang
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(srclang PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>)

if(UNIX)
  target_link_libraries(srclang PRIVATE m)
  target_link_options(srclang PRIVATE -static)
endif()

if(SRCLANG_BUILD_TESTS)
  enable_testing()

  add_test(
    NAME srclang_native_driver
    COMMAND
      ${CMAKE_COMMAND}
        -D SRCLANG_CLI=$<TARGET_FILE:srclang>
        -D SRCLANG_CC=${CMAKE_C_COMPILER}
        -D SRCLANG_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -D SRCLANG_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_asm.cmake)

endif()

install(TARGETS srclang
  RUNTIME DESTINATION bin)

install(DIRECTORY std/ DESTINATION share/srclang/std)

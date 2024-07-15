cmake_minimum_required(VERSION 3.10)
project(capsule
        VERSION "0.1"
        DESCRIPTION "Source Programming Language"
        LANGUAGES C)

find_library(READLINE readline)
if (READLINE)
        add_definitions(-DHAS_READLINE)
else()
        set(READLINE "")
endif()

include_directories(include
        ${CMAKE_BINARY_DIR}/generated)

file(READ "src/runtime.cap" RUNTIME_FILE)
string(REPLACE "\\" "\\\\" RUNTIME_FILE "${RUNTIME_FILE}")
string(REPLACE "\"" "\\\"" RUNTIME_FILE "${RUNTIME_FILE}")
string(REPLACE "\n" "\\n\"\n\"" RUNTIME_FILE "${RUNTIME_FILE}")
set(RUNTIME_FILE "\"${RUNTIME_FILE}\\n\"")
file(WRITE "${CMAKE_BINARY_DIR}/generated/runtime.h" "const char* RUNTIME = ${RUNTIME_FILE};\n\n")

add_executable(${PROJECT_NAME}
        src/builtin.c
        src/capsule.c
        src/eval.c
        src/error.c
        src/scope.c
        src/utils.c
        src/main.c
        src/read.c)

target_link_libraries(${PROJECT_NAME} PRIVATE
        ${READLINE})
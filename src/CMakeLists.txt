add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime.h"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/generate-runtime.sh" RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/runtime.cap ${CMAKE_CURRENT_BINARY_DIR}/runtime.h
        DEPENDS ${PROJECT_SOURCE_DIR}/scripts/generate-runtime.sh ${CMAKE_CURRENT_SOURCE_DIR}/runtime.cap
)

add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/logo.h"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/generate-runtime.sh" LOGO ${CMAKE_CURRENT_SOURCE_DIR}/logo.txt ${CMAKE_CURRENT_BINARY_DIR}/logo.h
        DEPENDS ${PROJECT_SOURCE_DIR}/scripts/generate-runtime.sh ${CMAKE_CURRENT_SOURCE_DIR}/logo.txt
)

add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include.h"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/generate-runtime.sh" CAPSULE_INCLUDE ${CMAKE_SOURCE_DIR}/include/capsule.h ${CMAKE_CURRENT_BINARY_DIR}/include.h
)


include_directories(${CMAKE_CURRENT_BINARY_DIR})


add_library(${PROJECT_NAME}_Shared STATIC
        builtin.c
        capsule.c
        eval.c
        lib.c
        print.c
        read.c
        scope.c
        ${CMAKE_CURRENT_BINARY_DIR}/runtime.h
        ${CMAKE_CURRENT_BINARY_DIR}/logo.h
        ${CMAKE_CURRENT_BINARY_DIR}/include.h
        memory.c)


find_library(FFI ffi)
if (FFI)
    target_compile_definitions(${PROJECT_NAME}_Shared PRIVATE -DHAS_FFI)
endif ()


target_link_libraries(${PROJECT_NAME}_Shared
        PUBLIC
        ${FFI})
target_link_options(${PROJECT_NAME}_Shared PUBLIC -rdynamic)

set_target_properties(${PROJECT_NAME}_Shared
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/capsule.h")

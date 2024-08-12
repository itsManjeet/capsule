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
        memory.c)


find_library(FFI ffi)
if (FFI)
    target_compile_definitions(${PROJECT_NAME}_Shared PRIVATE -DHAS_FFI)
endif ()

add_dependencies(${PROJECT_NAME}_Shared tinycc)

target_link_libraries(${PROJECT_NAME}_Shared
        PUBLIC
        ${FFI}
        tcc)


set_target_properties(${PROJECT_NAME}_Shared
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/capsule.h")

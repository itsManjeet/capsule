add_executable(${PROJECT_NAME}
        main.c)

find_library(READLINE readline)
if (READLINE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DHAS_READLINE)
endif ()


target_link_libraries(${PROJECT_NAME} PRIVATE
        ${READLINE}
        ${PROJECT_NAME}_Shared)
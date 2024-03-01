file(GLOB_RECURSE TestFiles "*.src")
add_executable(${PROJECT_NAME}_TEST runtest.cpp)
target_link_libraries(${PROJECT_NAME}_TEST ${PROJECT_NAME}Library)
foreach (testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMAND $<TARGET_FILE:${PROJECT_NAME}_TEST> ${testFile})
endforeach ()
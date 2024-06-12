file(GLOB_RECURSE TestFiles "*.src")

foreach (testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh $<TARGET_FILE:${PROJECT_NAME}> ${testFile})
endforeach ()
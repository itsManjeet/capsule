file(GLOB_RECURSE TestFiles "*.src")
foreach(testFile ${TestFiles})
    get_filename_component(testName ${testFile} NAME_WE)
    add_test(NAME ${testName}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMAND tests/run_test.sh ${PROJECT_BINARY_DIR}/${PROJECT_NAME} tests/${testName}.src)
endforeach()
file(GLOB_RECURSE SourceFiles "*.cpp" "*.h")
add_library(${PROJECT_NAME}Library SHARED ${SourceFiles})
set_target_properties(${PROJECT_NAME}Library
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        PUBLIC_HEADER "Language/Language.h;Language/Options.h;Value/Value.h;ByteCode/ByteCode.h;ByteCode/DebugInfo.h;ByteCode/Instructions.h")

install(TARGETS ${PROJECT_NAME}Library
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (WIN32)
    target_link_libraries(${PROJECT_NAME}Library PUBLIC wsock32 ws2_32)
endif ()
file(GLOB_RECURSE SourceFiles "*.cpp" "*.h")
add_library(${PROJECT_NAME}Library ${SourceFiles})
set_target_properties(${PROJECT_NAME}Library
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}")

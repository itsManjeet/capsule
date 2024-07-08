function(add_module id)
    add_library(${PROJECT_NAME}_${id} SHARED ${id}.cpp)
    set_target_properties(${PROJECT_NAME}_${id} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${id}"
        SUFFIX ".mod")
    target_include_directories(${PROJECT_NAME}_${id} PRIVATE ${CMAKE_SOURCE_DIR}/src)
endfunction(add_module)

file(GLOB_RECURSE ModuleFiles "*.src")

install(FILES ${ModuleFiles}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/SrcLang)
function(add_module id)
    add_library(${PROJECT_NAME}_${id} SHARED ${id}.cpp)
    set_target_properties(${PROJECT_NAME}_${id} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${id}"
        SUFFIX ".mod")
    target_include_directories(${PROJECT_NAME}_${id} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    install(TARGETS ${PROJECT_NAME}_${id}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/SrcLang
    )
endfunction(add_module)

add_module(sys)
add_module(math)

file(GLOB_RECURSE ModuleFiles "*.src")

install(FILES ${ModuleFiles}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/SrcLang)
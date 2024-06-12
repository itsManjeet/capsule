function(add_module id)
    add_library(${PROJECT_NAME}_${id} SHARED ${id}.cpp)
    set_target_properties(${PROJECT_NAME}_${id}
            PROPERTIES
            OUTPUT_NAME "${id}"
            PREFIX "" SUFFIX ".so")
    install(TARGETS ${PROJECT_NAME}_${id}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/SrcLang)
endfunction()

add_module(os)
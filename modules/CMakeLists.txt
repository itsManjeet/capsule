function(add_module id)
    add_library(Capsule_${id} SHARED ${id}.c)
    target_include_directories(Capsule_${id} PUBLIC ${PROJECT_NAME}_Shared)
    set_target_properties(Capsule_${id}
            PROPERTIES
            OUTPUT_NAME "${id}")
endfunction()


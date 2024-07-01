file(GLOB_RECURSE SourceFiles "*.cpp" "*.h")
add_executable(${PROJECT_NAME} ${SourceFiles})

set_target_properties(${PROJECT_NAME}
        PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        PUBLIC_HEADER "Value.h;ByteCode.h;DebugInfo.h;Instructions.h;Interpreter.h;Compiler.h;SymbolTable.h;MemoryManager.h")

target_link_options(${PROJECT_NAME} PRIVATE
        -rdynamic)
        
target_link_libraries(${PROJECT_NAME} PRIVATE
        ${FFI} ${DL})

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SrcLang)

add_library(${PROJECT_NAME}_LIB
        Value/Value.hxx Value/Value.cxx
        Compiler/SymbolTable/SymbolTable.cxx Compiler/SymbolTable/SymbolTable.hxx
        ByteCode/DebugInfo.hxx
        ByteCode/Instructions.cxx ByteCode/Instructions.hxx
        ByteCode/ByteCode.cxx ByteCode/ByteCode.hxx
        Value/Function.cxx Value/Function.hxx
        Interpreter/MemoryManager/MemoryManager.cxx Interpreter/MemoryManager/MemoryManager.hxx
        Builtins/Builtin.cxx Builtins/Builtin.hxx
        Language/Options.cxx Language/Options.hxx
        Compiler/Compiler.cxx Compiler/Compiler.hxx
        Interpreter/Interpreter.cxx Interpreter/Interpreter.hxx
        Language/Language.cxx Language/Language.hxx)

target_link_libraries(${PROJECT_NAME}_LIB ${DL} ${READLINE})
add_executable(${PROJECT_NAME} srclang.cxx)

set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME "${PROJECT_NAME}"
        SUFFIX "")
target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}_LIB)


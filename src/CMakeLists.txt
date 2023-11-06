add_executable(${PROJECT_NAME}
        Value/Value.hxx Value/Value.cxx
        Compiler/SymbolTable/SymbolTable.cxx Compiler/SymbolTable/SymbolTable.hxx
        ByteCode/DebugInfo.hxx
        ByteCode/Instructions.cxx ByteCode/Instructions.hxx
        ByteCode/ByteCode.cxx ByteCode/ByteCode.hxx
        Value/Function.cxx Value/Function.hxx
        Interpreter/MemoryManager/MemoryManager.cxx Interpreter/MemoryManager/MemoryManager.hxx
        Interpreter/Builtins/Builtin.cxx Interpreter/Builtins/Builtin.hxx
        Language/Options.cxx Language/Options.hxx
        Compiler/Compiler.cxx Compiler/Compiler.hxx
        Interpreter/Interpreter.cxx Interpreter/Interpreter.hxx
        Language/Language.cxx Language/Language.hxx
        srclang.cxx)

add_dependencies(${PROJECT_NAME} tinycc)
target_link_libraries(${PROJECT_NAME} ${DL} ${FFI} ${TCC})
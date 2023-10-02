#ifndef SRCLANG_LANGUAGE_HXX
#define SRCLANG_LANGUAGE_HXX

#include <filesystem>
#include <tuple>

#include "Compiler.hxx"
#include "Interpreter.hxx"
#include "Options.hxx"
#include "SymbolTable.hxx"
#include "Value.hxx"

namespace srclang {

    struct Language {
        MemoryManager memoryManager;
        Options options;
        SymbolTable symbolTable;

        SrcLangList globals;
        SrcLangList constants;

        Language();

        ~Language();

        void define(std::string const &id, Value value);

        size_t add_constant(Value value);

        Value register_object(Value value);

        Value resolve(std::string const &id);

        std::tuple<Value, ByteCode, std::shared_ptr<DebugInfo>>
        compile(std::string const &input, std::string const &filename);

        Value execute(std::string const &input, std::string const &filename);

        Value execute(ByteCode &code, const std::shared_ptr<DebugInfo> &debugInfo);

        Value execute(const std::filesystem::path &filename);

        Value call(Value callee, std::vector<Value> const &args);

        void appendSearchPath(std::string const &path);
    };

}  // namespace srclang

#endif

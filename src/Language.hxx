#ifndef SRCLANG_LANGUAGE_HXX
#define SRCLANG_LANGUAGE_HXX

#include "Value.hxx"
#include "Options.hxx"
#include "Compiler.hxx"
#include "Interpreter.hxx"
#include "SymbolTable.hxx"
#include <filesystem>
#include <libtcc.h>

namespace srclang {

    struct Language {
        MemoryManager memoryManager;
        Options options;
        SymbolTable symbolTable;

        SrcLangList globals;
        SrcLangList constants;

        TCCState *state{nullptr};

        std::string cc_code{};

        Language();

        void define(std::string const &id, Value value);

        Value resolve(std::string const &id);

        bool compile(std::string const &filename, std::optional<std::string> output);

        Value execute(std::string const &input, std::string const &filename);

        Value execute(ByteCode &code, const std::shared_ptr<DebugInfo> &debugInfo);

        Value execute(const std::filesystem::path& filename);

        Value call(Value callee, std::vector<Value> const &args);

        void appendSearchPath(std::string const &path);

    };

}

#endif

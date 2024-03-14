#ifndef SRCLANG_LANGUAGE_H
#define SRCLANG_LANGUAGE_H

#include <filesystem>
#include <tuple>

#include "Options.h"
#include "../Value/Value.h"
#include "../ByteCode/ByteCode.h"

namespace srclang {

    struct SymbolTable;
    struct MemoryManager;

    struct Language {
        MemoryManager *memoryManager;
        Options options;
        SymbolTable *symbolTable;

        SrcLangList globals;
        SrcLangList constants;

        Language();

        ~Language();

        void define(std::string const &id, Value value);

        [[nodiscard]] Value registerObject(Value value) const;

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

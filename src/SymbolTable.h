#ifndef SRCLANG_SYMBOLTABLE_H
#define SRCLANG_SYMBOLTABLE_H

#include "Value.h"
#include <optional>

namespace SrcLang {

#define SRCLANG_SYMBOL_SCOPE_LIST                                              \
    X(BUILTIN)                                                                 \
    X(GLOBAL)                                                                  \
    X(LOCAL)                                                                   \
    X(FREE)                                                                    \
    X(FUNCTION)                                                                \
    X(TYPE)

struct Symbol {
    std::wstring name{};
    enum Scope {
#define X(id) id,
        SRCLANG_SYMBOL_SCOPE_LIST
#undef X
    } scope{GLOBAL};
    int index{0};
};

static const std::vector<std::wstring> SRCLANG_SYMBOL_ID = {
        L"Builtin",
        L"Global",
        L"Local",
        L"Free",
        L"Function",
        L"Type",
};

class SymbolTable {
public:
    SymbolTable* parent{nullptr};
    std::map<std::wstring, Symbol> store;
    std::vector<Symbol> free;
    int definitions{0};

public:
    explicit SymbolTable(SymbolTable* parent = nullptr);

    Symbol define(const std::wstring& name);

    Symbol define(const std::wstring& name, int index);

    Symbol define(const Symbol& other);

    Symbol defineFun(const std::wstring& name);

    std::optional<Symbol> resolve(const std::wstring& name);
};
} // namespace SrcLang

#endif // SRCLANG_SYMBOLTABLE_H

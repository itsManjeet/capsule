#include "SymbolTable.h"

using namespace SrcLang;

Symbol SymbolTable::define(const std::wstring& name) {
    store[name] =
            Symbol{name, (parent == nullptr ? Symbol::GLOBAL : Symbol::LOCAL),
                    definitions++};
    return store[name];
}

Symbol SymbolTable::define(const std::wstring& name, int index) {
    store[name] = Symbol{name, Symbol::BUILTIN, index};
    return store[name];
}

Symbol SymbolTable::define(const Symbol& other) {
    free.push_back(other);
    auto sym = Symbol{other.name, Symbol::FREE, (int)free.size() - 1};
    store[other.name] = sym;
    return sym;
}

Symbol SymbolTable::define_fun(const std::wstring& name) {
    store[name] = Symbol{name, Symbol::FUNCTION, 0};
    return store[name];
}

std::optional<Symbol> SymbolTable::resolve(const std::wstring& name) {
    auto iter = store.find(name);
    if (iter != store.end()) { return iter->second; }
    if (parent != nullptr) {
        auto sym = parent->resolve(name);
        if (sym == std::nullopt) { return sym; }

        if (sym->scope == Symbol::Scope::GLOBAL ||
                sym->scope == Symbol::Scope::BUILTIN ||
                sym->scope == Symbol::Scope::TYPE) {
            return sym;
        }
        return define(*sym);
    }
    return std::nullopt;
}

SymbolTable::SymbolTable(SymbolTable* parent) : parent{parent} {}

#ifndef SRCLANG_FUNCTION_H
#define SRCLANG_FUNCTION_H

#include "DebugInfo.h"
#include "Instructions.h"
#include "Value.h"
#include <memory>

namespace SrcLang {

enum class FunctionType { Function, Method, Initializer, Native };

struct Function {
    FunctionType type{FunctionType::Function};
    std::wstring id;
    std::shared_ptr<Instructions> instructions{nullptr};
    int nlocals{0};
    int nparams{0};
    bool is_variadic{false};
    std::shared_ptr<DebugInfo> debug_info{nullptr};
};

struct Closure {
    Function* fun;
    std::vector<Value> free{0};
};

} // namespace SrcLang

#endif

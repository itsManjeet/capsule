#include "Interpreter.h"

#ifdef _WIN32
#    include <windows.h>
#    define RTLD_LAZY 0
#    define RTLD_LOCAL 0
void* dlopen(const char* path, int flags) {
    HMODULE handler = GetModuleHandle(path);
    return (void*)handler;
}

void* dlsym(void* handler, const char* str) {
    auto proc = GetProcAddress((HMODULE)handler, str);
    return (void*)proc;
}

const char* dlerror() { return "no error code on windows"; }

#else
#    include <dlfcn.h>
#endif

#include "Builtin.h"
#include "Compiler.h"
#include "SymbolTable.h"
#include <array>
#include <iostream>

using namespace SrcLang;

#ifdef WITH_FFI
#    include <ffi.h>
static std::map<Native::Type, ffi_type*> ctypes = {
        {Native::Type::i8, &ffi_type_sint8},
        {Native::Type::i16, &ffi_type_sint16},
        {Native::Type::i32, &ffi_type_sint32},
        {Native::Type::i64, &ffi_type_sint64},
        {Native::Type::u8, &ffi_type_uint8},
        {Native::Type::u16, &ffi_type_uint16},
        {Native::Type::u32, &ffi_type_uint32},
        {Native::Type::u64, &ffi_type_uint64},
        {Native::Type::f32, &ffi_type_float},
        {Native::Type::f64, &ffi_type_double},
        {Native::Type::ptr, &ffi_type_pointer},
};
#endif

void Interpreter::error(std::wstring const& msg) {
    if (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->debug_info == nullptr) {
        errStream << L"ERROR: " << msg << std::endl;
        return;
    }

    errStream << SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                         ->fun->debug_info->filename
              << ":"
              << SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                         ->fun->debug_info->lines[distance(
                                 SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                         ->fun->instructions->begin(),
                                 cp->fp->ip)]
              << std::endl;
    errStream << L"  ERROR: " << msg;
}

Interpreter* Interpreter::m_globalActive = nullptr;

Interpreter::Interpreter()
        : globals(65536), options({
                                  {L"VERSION", SRCLANG_VERSION},
                                  {L"GC_HEAP_GROW_FACTOR", 1.3f},
                                  {L"GC_INITIAL_TRIGGER", 10},
                                  {L"SEARCH_PATH", L""},
                                  {L"BYTECODE_DUMP", false},
                                  {L"EXPERIMENTAL_FEATURE", false},
                                  {L"DEBUG", false},
                                  {L"BREAK", false},
                                  {L"DUMP_AST", false},
                          }) {
    m_globalActive = this;
    for (auto b : builtins) { memoryManager.heap.push_back(b); }
    {
        int i = 0;
#define X(id) symbolTable.define(s2ws(#id), i++);
        SRCLANG_BUILTIN_LIST
#undef X
    }
    auto sym = symbolTable.define(L"true");
    globals.at(sym.index) = SRCLANG_VALUE_BOOL(true);

    sym = symbolTable.define(L"false");
    globals.at(sym.index) = SRCLANG_VALUE_BOOL(false);

    sym = symbolTable.define(L"null");
    globals.at(sym.index) = SRCLANG_VALUE_NULL;
}

Interpreter::~Interpreter() = default;

void Interpreter::graceFullExit() {
    do {
        if (cp->sp != cp->stack.begin()) --cp->sp;
        for (auto i = cp->fp->defers.rbegin(); i != cp->fp->defers.rend();
                ++i) {
            call(*i, {});
        }

        if (cp->fp == cp->frames.begin()) { break; }
        cp->sp = cp->fp->bp - 1;
        *cp->sp++ = SRCLANG_VALUE_NULL;
        --cp->fp;
    } while (true);
}

Value Interpreter::addObject(Value val) {
#ifdef SRCLANG_GC_DEBUG
    gc();
#else
    if (memoryManager.heap.size() > nextGc && nextGc < limitNextGc) {
        gc();
        memoryManager.heap.shrink_to_fit();
        nextGc =
                static_cast<int>(static_cast<float>(memoryManager.heap.size()) *
                                 gcHeapGrowFactor);
    }
#endif
    return memoryManager.addObject(val);
}

void Interpreter::gc() {
#ifdef SRCLANG_GC_DEBUG
    std::cout << "Total allocations: " << memoryManager.heap.size()
              << std::endl;
    std::cout << "gc begin:" << std::endl;
#endif
    auto curctxt = cp;
    memoryManager.unmark();
    while (true) {
        auto fp = curctxt->fp;
        memoryManager.mark(curctxt->stack.begin(), curctxt->sp);

        while (true) {
            memoryManager.mark(fp->closure);
            if (fp == curctxt->frames.begin()) break;
            fp--;
        }

        if (curctxt == contexts.begin()) break;
        curctxt--;
    }
    memoryManager.mark(
            globals.begin(), globals.begin() + symbolTable.definitions);
    memoryManager.mark(constants.begin(), constants.end());
    memoryManager.mark(builtins.begin(), builtins.end());
    memoryManager.sweep();
#ifdef SRCLANG_GC_DEBUG
    std::cout << "gc end:" << std::endl;
    std::cout << "Total allocations: " << memoryManager.heap.size()
              << std::endl;
#endif
}

bool Interpreter::isEqual(Value a, Value b) {
    auto aType = SRCLANG_VALUE_GET_TYPE(a);
    auto bType = SRCLANG_VALUE_GET_TYPE(b);
    if (aType != bType) return false;
    if (SRCLANG_VALUE_IS_OBJECT(a)) return a == b;
    switch (aType) {
    case ValueType::Error:
    case ValueType::String: {
        auto* aBuffer = SRCLANG_VALUE_AS_STRING(a);
        auto* bBuffer = SRCLANG_VALUE_AS_STRING(b);
        return !wcscmp(aBuffer, bBuffer);
    }

    case ValueType::List: {
        auto* aList = SRCLANG_VALUE_AS_LIST(a);
        auto* bList = SRCLANG_VALUE_AS_LIST(b);
        if (aList->size() != bList->size()) return false;
        for (int i = 0; i < aList->size(); i++) {
            if (!isEqual(aList->at(i), bList->at(i))) return false;
        }
        return true;
    }

    case ValueType::Map: {
        auto* aMap = SRCLANG_VALUE_AS_MAP(a);
        auto* bMap = SRCLANG_VALUE_AS_MAP(b);
        if (aMap->size() != bMap->size()) return false;
        for (auto const& i : *aMap) {
            auto iter = bMap->find(i.first);
            if (iter == bMap->end()) return false;
            if (!isEqual(iter->second, i.second)) return false;
        }
        return true;
    }
    default: return false;
    }
    return false;
}

bool Interpreter::unary(Value a, OpCode op) {
    if (OpCode::NOT == op) {
        *cp->sp++ = SRCLANG_VALUE_BOOL(isFalsy(a));
        return true;
    }
    if (SRCLANG_VALUE_IS_NUMBER(a)) {
        switch (op) {
        case OpCode::NEG:
            *cp->sp++ = SRCLANG_VALUE_NUMBER(-SRCLANG_VALUE_AS_NUMBER(a));
            break;
        default:
            error("unexpected unary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] +
                    "'");
            return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(a) == ValueType::String) {
        switch (op) {
        default:
            error("unexpected unary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] +
                    "'");
            return false;
        }
    } else {
        error("ERROR: unhandled unary operation for value of type " +
                SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                        SRCLANG_VALUE_GET_TYPE(a))] +
                "'");
        return false;
    }

    return true;
}

bool Interpreter::binary(Value lhs, Value rhs, OpCode op) {
    if ((op == OpCode::NE || op == OpCode::EQ) &&
            SRCLANG_VALUE_GET_TYPE(lhs) != SRCLANG_VALUE_GET_TYPE(rhs)) {
        *cp->sp++ = op == OpCode::NE ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
        return true;
    }

    if (op == OpCode::OR) {
        *cp->sp++ = (isFalsy(lhs) ? rhs : lhs);
        return true;
    }

    if (SRCLANG_VALUE_IS_NULL(lhs) && SRCLANG_VALUE_IS_NULL(rhs)) {
        *cp->sp++ = op == OpCode::EQ ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
        return true;
    }

    if (SRCLANG_VALUE_IS_TYPE(lhs)) {
        auto a = SRCLANG_VALUE_AS_TYPE(lhs);
        if (!SRCLANG_VALUE_IS_TYPE(rhs)) {
            error("can't apply binary operation '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "' and '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                    "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_TYPE(rhs);
        switch (op) {
        case OpCode::EQ: *cp->sp++ = SRCLANG_VALUE_BOOL(a == b); break;
        case OpCode::NE: *cp->sp++ = SRCLANG_VALUE_BOOL(a != b); break;
        default:
            error("ERROR: unexpected binary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
            return false;
        }
    } else if (SRCLANG_VALUE_IS_NUMBER(lhs)) {
        auto a = SRCLANG_VALUE_AS_NUMBER(lhs);
        if (!SRCLANG_VALUE_IS_NUMBER(rhs)) {
            error("can't apply binary operation '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "' and '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                    "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_NUMBER(rhs);
        switch (op) {
        case OpCode::ADD: *cp->sp++ = SRCLANG_VALUE_NUMBER(a + b); break;
        case OpCode::SUB: *cp->sp++ = SRCLANG_VALUE_NUMBER(a - b); break;
        case OpCode::MUL: *cp->sp++ = SRCLANG_VALUE_NUMBER(a * b); break;
        case OpCode::DIV: *cp->sp++ = SRCLANG_VALUE_NUMBER(a / b); break;
        case OpCode::EQ:
            *cp->sp++ = a == b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::NE:
            *cp->sp++ = a != b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::LT:
            *cp->sp++ = a < b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::LE:
            *cp->sp++ = a <= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::GT:
            *cp->sp++ = a > b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::GE:
            *cp->sp++ = a >= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            break;
        case OpCode::LSHIFT:
            *cp->sp++ = SRCLANG_VALUE_NUMBER((long)a >> (long)b);
            break;
        case OpCode::RSHIFT:
            *cp->sp++ = SRCLANG_VALUE_NUMBER((long)a << (long)b);
            break;
        case OpCode::MOD:
            *cp->sp++ = SRCLANG_VALUE_NUMBER((long)a % (long)b);
            break;
        case OpCode::LOR:
            *cp->sp++ = SRCLANG_VALUE_NUMBER((long)a | (long)b);
            break;
        case OpCode::LAND:
            *cp->sp++ = SRCLANG_VALUE_NUMBER((long)a & (long)b);
            break;
        default:
            error("ERROR: unexpected binary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
            return false;
        }
    } else if (SRCLANG_VALUE_IS_BOOL(lhs)) {
        bool a = SRCLANG_VALUE_AS_BOOL(lhs);
        if (!SRCLANG_VALUE_IS_BOOL(rhs)) {
            error("can't apply binary operation '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "' and '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                    "'");
            return false;
        }
        bool b = SRCLANG_VALUE_AS_BOOL(rhs);
        switch (op) {
        case OpCode::EQ: *cp->sp++ = SRCLANG_VALUE_BOOL(a == b); break;
        case OpCode::NE: *cp->sp++ = SRCLANG_VALUE_BOOL(a != b); break;
        case OpCode::AND: *cp->sp++ = SRCLANG_VALUE_BOOL(a && b); break;
        case OpCode::OR: *cp->sp++ = SRCLANG_VALUE_BOOL(a || b); break;
        default:
            error("ERROR: unexpected binary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
            return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::String) {
        auto* a = SRCLANG_VALUE_AS_STRING(lhs);
        if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::String) {
            error("can't apply binary operation '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "' and '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                    "'");
            return false;
        }
        auto* b = SRCLANG_VALUE_AS_STRING(rhs);

        switch (op) {
        case OpCode::ADD: {
            auto a_size = wcslen(a);
            auto b_size = wcslen(b);
            auto size = a_size + b_size;

            auto* buf = (wchar_t*)malloc((size + 1) * sizeof(wchar_t));
            if (buf == nullptr) {
                throw std::runtime_error(
                        "malloc() failed for string + string, " +
                        std::string(strerror(errno)));
            }
            wcscpy(buf, a);
            wcscat(buf, b);

            *cp->sp++ = addObject(SRCLANG_VALUE_STRING(buf));
        } break;
        case OpCode::EQ: {
            *cp->sp++ = wcscmp(a, b) == 0 ? SRCLANG_VALUE_TRUE
                                          : SRCLANG_VALUE_FALSE;
        } break;
        case OpCode::NE: {
            *cp->sp++ = wcscmp(a, b) != 0 ? SRCLANG_VALUE_TRUE
                                          : SRCLANG_VALUE_FALSE;
        } break;
        case OpCode::GT:
            *cp->sp++ = SRCLANG_VALUE_BOOL(wcslen(a) > wcslen(b));
            break;
        case OpCode::LT:
            *cp->sp++ = SRCLANG_VALUE_BOOL(wcslen(a) < wcslen(b));
            break;
        default:
            error("ERROR: unexpected binary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "'");
            return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::List) {
        auto a = reinterpret_cast<SrcLangList*>(
                SRCLANG_VALUE_AS_OBJECT(lhs)->pointer);
        if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::List) {
            error("can't apply binary operation '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "' and '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                    "'");
            return false;
        }

        auto b = reinterpret_cast<SrcLangList*>(
                SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);
        switch (op) {
        case OpCode::ADD: {
            auto c = new SrcLangList(a->begin(), a->end());
            c->insert(c->end(), b->begin(), b->end());
            *cp->sp++ = SRCLANG_VALUE_LIST(c);
            addObject(*(cp->sp - 1));
        } break;
        case OpCode::GT:
            *cp->sp++ = SRCLANG_VALUE_BOOL(a->size() > b->size());
            break;
        case OpCode::LT:
            *cp->sp++ = SRCLANG_VALUE_BOOL(a->size() < b->size());
            break;
        default:
            error("ERROR: unexpected binary operator '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                    SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                    "'");
            return false;
        }
    } else {
        error("ERROR: unsupported binary operator '" +
                SRCLANG_OPCODE_ID[int(op)] + "' for type '" +
                SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                        SRCLANG_VALUE_GET_TYPE(lhs))] +
                "'");
        return false;
    }

    return true;
}

bool Interpreter::isFalsy(Value val) {
    return SRCLANG_VALUE_IS_NULL(val) ||
           (SRCLANG_VALUE_IS_BOOL(val) &&
                   SRCLANG_VALUE_AS_BOOL(val) == false) ||
           (SRCLANG_VALUE_IS_NUMBER(val) &&
                   SRCLANG_VALUE_AS_NUMBER(val) == 0) ||
           (SRCLANG_VALUE_IS_OBJECT(val) &&
                   SRCLANG_VALUE_AS_OBJECT(val)->type == ValueType::Error);
}

void Interpreter::printStack() {
    if (debug) {
        std::wcout << L"  ";
        for (auto i = cp->stack.begin(); i != cp->sp; ++i) {
            std::wcout << L"[" << SRCLANG_VALUE_GET_STRING(*i) << L"] ";
        }
        std::wcout << std::endl;
    }
}

bool Interpreter::callClosure(Value callee, uint8_t count) {
    auto closure = reinterpret_cast<Closure*>(
            SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    if (closure->fun->is_variadic) {
        if (count < closure->fun->nparams - 1) {
            error("expected atleast '" +
                    std::to_string(closure->fun->nparams - 1) + "' but '" +
                    std::to_string(count) + "' provided");
            return false;
        }
        auto v_arg_begin = (cp->sp - (count - (closure->fun->nparams - 1)));
        auto v_arg_end = cp->sp;
        SrcLangList* var_args;
        auto dist = distance(v_arg_begin, v_arg_end);
        if (dist == 0) {
            var_args = new SrcLangList();
        } else {
            var_args = new SrcLangList(v_arg_begin, v_arg_end);
        }
        auto var_val = SRCLANG_VALUE_LIST(var_args);
        addObject(var_val);
        *(cp->sp - (count - (closure->fun->nparams - 1))) = var_val;
        cp->sp = (cp->sp - (count - closure->fun->nparams));
        count = closure->fun->nparams;
    }

    if (count != closure->fun->nparams) {
        error("expected '" + std::to_string(closure->fun->nparams) + "' but '" +
                std::to_string(count) + "' provided");
        return false;
    }

    cp->fp++;
    cp->fp->closure = callee;
    cp->fp->ip = SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                         ->fun->instructions->begin();
    cp->fp->bp = (cp->sp - count);
    cp->sp = cp->fp->bp +
             SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->nlocals;
    return true;
}

bool Interpreter::callBuiltin(Value callee, uint8_t count) {
    auto builtin =
            reinterpret_cast<Builtin>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    std::vector<Value> args(cp->sp - count, cp->sp);
    cp->sp -= count + 1;
    Value result;

    try {
        result = builtin(args, this);
    } catch (std::exception const& exception) {
        error(exception.what());
        return false;
    }

    if (SRCLANG_VALUE_IS_OBJECT(result)) addObject(result);
    *cp->sp++ = result;
    return true;
}

bool Interpreter::callNative(Value callee, uint8_t count) {
    Value result_value;
#ifdef WITH_FFI
    auto native = SRCLANG_VALUE_AS_NATIVE(callee);

    if (count != native->parameters.size()) {
        error("expected '" + std::to_string(native->parameters.size()) +
                "' but '" + std::to_string(count) + "' provided");
        return false;
    }

    void* handler = dlsym(nullptr, ws2s(native->id).c_str());
    if (handler == nullptr) {
        error(dlerror());
        return false;
    }

    ffi_cif cif;
    void* values[count];
    ffi_type* types[count];
    std::vector<unsigned long> unsigned_value_holder(count);
    std::vector<long> signed_value_holder(count);
    std::vector<double> float_value_holder(count);

    int j = 0;
    for (auto i = cp->sp - count; i != cp->sp; i++, j++) {
        auto type = SRCLANG_VALUE_GET_TYPE(*i);

        switch (type) {
        case ValueType::Null:
            values[j] = nullptr;
            types[j] = &ffi_type_pointer;
            break;

        case ValueType::Number: {
            values[j] = &(*i);
            auto t = native->parameters[j];
            if ((int)t >= (int)Native::Type::i8 &&
                    (int)t <= (int)Native::Type::i64) {
                auto value = SRCLANG_VALUE_AS_NUMBER(*i);
                signed_value_holder[j] = static_cast<long>(value);
                values[j] = &signed_value_holder[j];
            } else if ((int)t >= (int)Native::Type::u8 &&
                       (int)t <= (int)Native::Type::u64) {
                auto value = SRCLANG_VALUE_AS_NUMBER(*i);
                unsigned_value_holder[j] = static_cast<unsigned long>(value);
                values[j] = &unsigned_value_holder[j];
            } else {
                auto value = SRCLANG_VALUE_AS_NUMBER(*i);
                float_value_holder[j] = value;
                values[j] = &float_value_holder[j];
            }

            types[j] = ctypes[native->parameters[j]];
        } break;

        case ValueType::Boolean:
            values[j] = &(*i);
            types[j] = &ffi_type_sint;
            break;

        default:
            values[j] = &SRCLANG_VALUE_AS_OBJECT(*i)->pointer;
            types[j] = &ffi_type_pointer;
            break;
        }
    }

    ffi_type* ret = ctypes[native->ret];

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, count, ret, types) != FFI_OK) {
        error("ffi_prep_cif() failed");
        return false;
    }

    ffi_arg result;

    ffi_call(&cif, FFI_FN(handler), &result, values);

    switch (native->ret) {
    case Native::Type::i8:
    case Native::Type::i16:
    case Native::Type::i32:
    case Native::Type::i64:
        result_value = SRCLANG_VALUE_NUMBER(static_cast<double>((long)result));
        break;
    case Native::Type::u8:
    case Native::Type::u16:
    case Native::Type::u32:
    case Native::Type::u64:
        result_value = SRCLANG_VALUE_NUMBER(
                static_cast<double>((unsigned long)result));
        break;
    case Native::Type::ptr:
        if ((void*)result == nullptr) {
            result_value = SRCLANG_VALUE_NULL;
        } else {
            result_value = SRCLANG_VALUE_POINTER((void*)result);
        }
        break;
    // case Native::Type::val:
    // std::cout << "NATIVE: " << result << std::endl;
    // break;
    default:
        error("ERROR: unsupported return type '" +
                SRCLANG_VALUE_TYPE_ID[int(native->ret)] + "'");
        return false;
    }
#else
    error("FFI calling not enabled");
    return false;
#endif

    cp->sp -= count + 1;
    *cp->sp++ = result_value;
    return true;
}

bool Interpreter::callTypecastNum(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(cp->sp - count);
    cp->sp -= count + 1;
    if (SRCLANG_VALUE_IS_OBJECT(val) &&
            SRCLANG_VALUE_AS_OBJECT(val)->type == ValueType::String) {
        try {
            *cp->sp++ = SRCLANG_VALUE_NUMBER(
                    std::stod(SRCLANG_VALUE_AS_STRING(val)));
        } catch (...) {
            error("invalid typecast str -> num");
            return false;
        }
    } else {
        error("invalid typecast to num");
        return false;
    }
    return true;
}

bool Interpreter::callTypecastString(uint8_t count) {
    Value result;
    if (count == 1) {
        Value val = *(cp->sp - count);
        switch (SRCLANG_VALUE_GET_TYPE(val)) {
        case ValueType::Pointer: {
            result = SRCLANG_VALUE_STRING(wcsdup(SRCLANG_VALUE_AS_STRING(val)));
        } break;

        case ValueType::List: {
            auto list = SRCLANG_VALUE_AS_LIST(val);
            std::wstring buf;
            for (auto i : *list) { buf += SRCLANG_VALUE_GET_STRING(i); }
            result = SRCLANG_VALUE_STRING(wcsdup(buf.c_str()));
        } break;

        default:
            result = SRCLANG_VALUE_STRING(
                    wcsdup(SRCLANG_VALUE_GET_STRING(val).c_str()));
            break;
        }
    } else {
        std::wstring buf;
        for (auto i = cp->sp - count; i != cp->sp; i++) {
            buf += SRCLANG_VALUE_GET_STRING(*i);
        }
        result = SRCLANG_VALUE_STRING(wcsdup(buf.c_str()));
    }

    cp->sp -= count + 1;
    *cp->sp++ = result;
    return true;
}

bool Interpreter::callTypecastError(uint8_t count) {
    std::wstring buf;
    for (auto i = cp->sp - count; i != cp->sp; i++) {
        buf += SRCLANG_VALUE_GET_STRING(*i);
    }
    cp->sp -= count + 1;
    *cp->sp++ = SRCLANG_VALUE_ERROR(wcsdup(buf.c_str()));
    return true;
}

bool Interpreter::callTypecastBool(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(cp->sp - count);
    cp->sp -= count + 1;
    *cp->sp++ = SRCLANG_VALUE_BOOL(!isFalsy(val));
    return true;
}

bool Interpreter::callTypecastType(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(cp->sp - count);
    cp->sp -= count + 1;
    *cp->sp++ = SRCLANG_VALUE_TYPE(SRCLANG_VALUE_GET_TYPE(val));
    return true;
}

bool Interpreter::callTypecast(Value callee, uint8_t count) {
    auto type = SRCLANG_VALUE_AS_TYPE(callee);
    switch (type) {
    case ValueType::Number: return callTypecastNum(count);
    case ValueType::Type: return callTypecastType(count);
    case ValueType::Boolean: return callTypecastBool(count);
    case ValueType::String: return callTypecastString(count);
    case ValueType::Error:
        return callTypecastError(count);
        // case ValueType::Function:
        //     return call_typecast_function(count);
    default: error("invalid typecast"); return false;
    }
}

bool Interpreter::callMap(Value callee, uint8_t count) {
    auto container = (SrcLangMap*)SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
    auto callback = container->find(L"__call__");
    if (callback == container->end()) {
        error("'__call__' is not defined in container");
        return false;
    }
    auto bounded_value =
            SRCLANG_VALUE_BOUNDED((new BoundedValue{callee, callback->second}));
    addObject(bounded_value);
    *(cp->sp - count - 1) = bounded_value;
    return callBounded(bounded_value, count);
}

bool Interpreter::callBounded(Value callee, uint8_t count) {
    auto bounded = (BoundedValue*)SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
    *(cp->sp - count - 1) = bounded->value;
    for (auto i = cp->sp; i != cp->sp - count; i--) { *i = *(i - 1); }
    *(cp->sp - count) = bounded->parent;
    cp->sp++;
    if (debug) {
        std::cout << "BOUNDED STACK" << std::endl;
        printStack();
    }

    return call(count + 1);
}

bool Interpreter::call(uint8_t count) {
    auto callee = *(cp->sp - 1 - count);
    if (SRCLANG_VALUE_IS_TYPE(callee)) {
        return callTypecast(callee, count);
    } else if (SRCLANG_VALUE_IS_OBJECT(callee)) {
        switch (SRCLANG_VALUE_AS_OBJECT(callee)->type) {
        case ValueType::Closure: return callClosure(callee, count);
        case ValueType::Native: return callNative(callee, count);
        case ValueType::Builtin: return callBuiltin(callee, count);
        case ValueType::Bounded: return callBounded(callee, count);
        case ValueType::Map: return callMap(callee, count);
        default:
            error(L"ERROR: can't call object '" + SRCLANG_VALUE_DEBUG(callee) +
                    L"'");
            return false;
        }
    }
    error(L"ERROR: can't call value of type '" + SRCLANG_VALUE_DEBUG(callee) +
            L"'");
    return false;
}

bool Interpreter::run() {
    debug = getOption<bool>(L"DEBUG");
    break_ = getOption<bool>(L"BREAK");

    if (getOption<bool>(L"DUMP_AST")) {
        ByteCode::debug(std::wcout,
                *SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->instructions,
                constants);
    }

    while (true) {
        if (debug) {
            if (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->debug_info !=
                    nullptr) {
                std::wcout
                        << SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                   ->fun->debug_info->filename
                        << ":"
                        << SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                   ->fun->debug_info->lines[distance(
                                           SRCLANG_VALUE_AS_CLOSURE(
                                                   cp->fp->closure)
                                                   ->fun->instructions->begin(),
                                           cp->fp->ip)]
                        << std::endl;
            }

            std::wcout << "  ";
            for (auto i = cp->stack.begin(); i != cp->sp; ++i) {
                std::wcout << "[" << SRCLANG_VALUE_DEBUG(*i) << "] ";
            }
            std::wcout << std::endl;
            std::wcout << ">> ";
            ByteCode::debug(*SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                     ->fun->instructions,
                    constants,
                    std::distance(SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                          ->fun->instructions->begin(),
                            cp->fp->ip),
                    std::wcout);
            std::wcout << std::endl;

            if (break_) std::cin.get();
        }
        auto inst = static_cast<OpCode>(*cp->fp->ip++);
        switch (inst) {
        case OpCode::CONST_: *cp->sp++ = constants[*cp->fp->ip++]; break;
        case OpCode::CONST_INT: *cp->sp++ = (*cp->fp->ip++); break;
        case OpCode::CONST_FALSE: *cp->sp++ = SRCLANG_VALUE_FALSE; break;
        case OpCode::CONST_TRUE: *cp->sp++ = SRCLANG_VALUE_TRUE; break;
        case OpCode::CONST_NULL: *cp->sp++ = SRCLANG_VALUE_NULL; break;
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::EQ:
        case OpCode::NE:
        case OpCode::LT:
        case OpCode::LE:
        case OpCode::GT:
        case OpCode::GE:
        case OpCode::LSHIFT:
        case OpCode::RSHIFT:
        case OpCode::AND:
        case OpCode::OR:
        case OpCode::LAND:
        case OpCode::LOR:
        case OpCode::MOD: {
            auto b = *--(cp->sp);
            auto a = *--(cp->sp);
            if (!binary(a, b, inst)) { return false; }
        } break;

        case OpCode::COMMAND:
        case OpCode::NOT:
        case OpCode::NEG: {
            if (!unary(*--(cp->sp), inst)) { return false; }
        } break;

        case OpCode::STORE: {
            auto scope = Symbol::Scope(*cp->fp->ip++);
            int pos = *cp->fp->ip++;

            switch (scope) {
            case Symbol::Scope::LOCAL:
                *(cp->fp->bp + pos) = *(cp->sp - 1);
                break;
            case Symbol::Scope::GLOBAL:
                if (pos >= globals.size()) {
                    error("GLOBALS SYMBOLS OVERFLOW");
                    return false;
                }
                globals[pos] = *(cp->sp - 1);
                break;
            default:
                error("Invalid STORE operation on '" +
                        ws2s(SRCLANG_SYMBOL_ID[int(scope)]) + "'");
                return false;
            }
        } break;

        case OpCode::LOAD: {
            auto scope = Symbol::Scope(*cp->fp->ip++);
            int pos = *cp->fp->ip++;
            switch (scope) {
            case Symbol::Scope::LOCAL: *cp->sp++ = *(cp->fp->bp + pos); break;
            case Symbol::Scope::GLOBAL: *cp->sp++ = globals[pos]; break;
            case Symbol::Scope::BUILTIN: *cp->sp++ = builtins[pos]; break;
            case Symbol::Scope::TYPE:
                *cp->sp++ = SRCLANG_VALUE_TYPES[pos];
                break;
            case Symbol::Scope::FREE:
                *cp->sp++ =
                        SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->free[pos];
                break;
            case Symbol::Scope::FUNCTION: *cp->sp++ = cp->fp->closure; break;
            default:
                error("ERROR: can't load value of scope '" +
                        ws2s(SRCLANG_SYMBOL_ID[int(scope)]) + "'");
                return false;
            }
        } break;

        case OpCode::CLOSURE: {
            int funIndex = *cp->fp->ip++;
            int nfree = *cp->fp->ip++;

            auto constant = constants[funIndex];
            auto fun = (Function*)SRCLANG_VALUE_AS_OBJECT(constant)->pointer;
            auto frees = std::vector<Value>(cp->sp - nfree, cp->sp);
            cp->sp -= nfree;
            *cp->sp++ =
                    addObject(SRCLANG_VALUE_CLOSURE((new Closure{fun, frees})));
        } break;

        case OpCode::CALL: {
            int count = *cp->fp->ip++;
            if (!call(count)) { return false; }
        } break;

        case OpCode::POP: {
            if (cp->sp == cp->stack.begin()) {
                error("Stack-underflow");
                return false;
            }
            *--(cp->sp);
        } break;

        case OpCode::PACK: {
            auto size = *cp->fp->ip++;
            auto list = new std::vector<Value>(cp->sp - size, cp->sp);
            cp->sp -= size;
            auto list_value = SRCLANG_VALUE_LIST(list);
            addObject(list_value);
            *cp->sp++ = list_value;
        } break;

        case OpCode::MAP: {
            auto size = *cp->fp->ip++;
            auto map = new SrcLangMap();
            for (auto i = cp->sp - (size * 2); i != cp->sp; i += 2) {
                map->insert({SRCLANG_VALUE_AS_STRING(*i), *(i + 1)});
            }
            cp->sp -= (size * 2);
            *cp->sp++ = SRCLANG_VALUE_MAP(map);
            addObject(*(cp->sp - 1));
        } break;

        case OpCode::INDEX: {
            auto count = *cp->fp->ip++;
            Value pos, end_idx;
            switch (count) {
            case 1: pos = *--(cp->sp); break;
            case 2:
                end_idx = *--(cp->sp);
                pos = *--(cp->sp);
                if (!(SRCLANG_VALUE_IS_NUMBER(pos) &&
                            SRCLANG_VALUE_IS_NUMBER(end_idx))) {
                    error("invalid INDEX for range");
                    return false;
                }
                break;
            default: error("invalid INDEX count"); return false;
            }
            auto container = *--(cp->sp);
            if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                auto* buffer = SRCLANG_VALUE_AS_STRING(container);
                int index = SRCLANG_VALUE_AS_NUMBER(pos);
                int len = wcslen(buffer);
                switch (count) {
                case 1: {
                    if (len <= index || index < 0) {
                        *cp->sp++ = SRCLANG_VALUE_NULL;
                    } else {
                        *cp->sp++ = SRCLANG_VALUE_STRING(
                                wcsdup(std::wstring(1, buffer[index]).c_str()));
                    }
                } break;
                case 2: {
                    int end = SRCLANG_VALUE_AS_NUMBER(end_idx);
                    if (index < 0) index = len + index;
                    if (end < 0) end = len + end + 1;
                    if (end - index < 0 || end - index >= len) {
                        *cp->sp++ = SRCLANG_VALUE_NULL;
                    } else {
                        auto* buf = (wchar_t*)malloc(
                                sizeof(wchar_t) * (end - index + 1));
                        memcpy(buf, buffer + index, end - index);

                        buf[end - index] = '\0';
                        *cp->sp++ = SRCLANG_VALUE_STRING(buf);

                        addObject(*(cp->sp - 1));
                    }
                } break;
                default:
                    error("Invalid INDEX range operation for string");
                    return false;
                }
            } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::List &&
                       SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                std::vector<Value> list =
                        *(std::vector<Value>*)SRCLANG_VALUE_AS_OBJECT(container)
                                 ->pointer;

                int index = SRCLANG_VALUE_AS_NUMBER(pos);
                switch (count) {
                case 1:
                    if (list.size() <= index || index < 0) {
                        *cp->sp++ = SRCLANG_VALUE_NULL;
                    } else {
                        *cp->sp++ = list[index];
                    }

                    break;
                case 2: {
                    int end = SRCLANG_VALUE_AS_NUMBER(end_idx);
                    if (index < 0) index = list.size() + index;
                    if (end < 0) end = list.size() + end + 1;
                    if (end - index < 0) {
                        error("Invalid range value");
                        return false;
                    }
                    auto values = new SrcLangList(end - index);
                    for (int i = index; i < end; i++) {
                        values->at(i) = builtin_clone({list[i]}, this);
                    }
                    *cp->sp++ = SRCLANG_VALUE_LIST(values);
                    addObject(*(cp->sp - 1));
                } break;
                default:
                    error("Invalid INDEX range operation for list");
                    return false;
                }
            } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::Map &&
                       SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                auto map = *SRCLANG_VALUE_AS_MAP(container);
                auto buf = SRCLANG_VALUE_AS_STRING(pos);
                auto get_index_callback = map.find(L"__index__");
                if (get_index_callback == map.end()) {
                    auto idx = map.find(buf);
                    if (idx == map.end()) {
                        *cp->sp++ = SRCLANG_VALUE_NULL;
                    } else {
                        *cp->sp++ = idx->second;
                    }
                } else {
                    auto bounded_value =
                            SRCLANG_VALUE_BOUNDED((new BoundedValue{
                                    container, get_index_callback->second}));
                    addObject(bounded_value);

                    *cp->sp++ = bounded_value;
                    *cp->sp++ = pos;
                    if (!callBounded(bounded_value, 1)) { return false; }
                }
            } else {
                error(L"InvalidOperation b/w '" + SRCLANG_VALUE_DEBUG(pos) +
                        L"' and '" + SRCLANG_VALUE_DEBUG(container) + L"'");
                return false;
            }
        } break;

        case OpCode::SET: {
            auto val = *--(cp->sp);
            auto pos = *--(cp->sp);
            auto container = *--(cp->sp);
            if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                auto idx = SRCLANG_VALUE_AS_NUMBER(pos);
                auto* buf = SRCLANG_VALUE_AS_STRING(container);
                int size = wcslen(buf);
                if (idx < 0 || size <= idx) {
                    error("out of bound");
                    return false;
                }
                if (SRCLANG_VALUE_IS_NUMBER(val)) {
                    buf = (wchar_t*)realloc(buf, size);
                    if (buf == nullptr) {
                        error("out of memory");
                        return false;
                    }
                    buf[long(idx)] = (char)SRCLANG_VALUE_AS_NUMBER(val);
                } else if (SRCLANG_VALUE_GET_TYPE(val) == ValueType::String) {
                    wchar_t* b = (wchar_t*)SRCLANG_VALUE_AS_STRING(val);
                    buf[long(idx)] = *b;
                } else {
                    error("can't SET '" +
                            SRCLANG_VALUE_TYPE_ID[int(
                                    SRCLANG_VALUE_GET_TYPE(val))] +
                            "' to string");
                    return true;
                }
            } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::List &&
                       SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                auto list = reinterpret_cast<SrcLangList*>(
                        SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                list->at(SRCLANG_VALUE_AS_NUMBER(pos)) = val;
            } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::Map &&
                       SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                auto map = SRCLANG_VALUE_AS_MAP(container);
                auto buf = SRCLANG_VALUE_AS_STRING(pos);
                auto set_index_callback = map->find(L"__set_index__");
                if (set_index_callback == map->end()) {
                    if (map->find(buf) == map->end()) {
                        map->insert({buf, val});
                    } else {
                        map->at(buf) = val;
                    }
                } else {
                    auto bounded_value =
                            SRCLANG_VALUE_BOUNDED((new BoundedValue{
                                    container, set_index_callback->second}));
                    addObject(bounded_value);

                    *cp->sp++ = bounded_value;
                    *cp->sp++ = pos;
                    *cp->sp++ = val;
                    if (!callBounded(bounded_value, 2)) { return false; }
                }
            } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                               ValueType::Pointer &&
                       SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                int idx = SRCLANG_VALUE_AS_NUMBER(pos);
                auto* buf = SRCLANG_VALUE_AS_STRING(container);
                wchar_t value = SRCLANG_VALUE_AS_NUMBER(val);
                buf[idx] = value;
            } else {
                error("invalid SET operation for '" +
                        SRCLANG_VALUE_TYPE_ID[int(
                                SRCLANG_VALUE_GET_TYPE(container))] +
                        "' and '" +
                        SRCLANG_VALUE_TYPE_ID[int(
                                SRCLANG_VALUE_GET_TYPE(pos))] +
                        "'");
                return false;
            }
            *cp->sp++ = container;
        } break;

        case OpCode::SIZE: {
            auto val = *--(cp->sp);
            if (!SRCLANG_VALUE_IS_OBJECT(val)) {
                error("container in loop is not iterable");
                return false;
            }
            auto obj = SRCLANG_VALUE_AS_OBJECT(val);
            switch (obj->type) {
            case ValueType::String:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(
                        wcslen(SRCLANG_VALUE_AS_STRING(val)));
                break;
            case ValueType::List:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(
                        ((SrcLangList*)obj->pointer)->size());
                break;
            default:
                error("container '" + ws2s(SRCLANG_VALUE_GET_STRING(val)) +
                        "' is not a iterable object");
                return false;
            }
        } break;

        case OpCode::RET: {
            auto value = *--(cp->sp);
            for (auto i = cp->fp->defers.rbegin(); i != cp->fp->defers.rend();
                    ++i)
                call(*i, {});

            if (cp->fp == cp->frames.begin()) {
                *cp->sp++ = value;
                return true;
            }

            cp->sp = cp->fp->bp - 1;
            --cp->fp;
            *cp->sp++ = value;
        } break;

        case OpCode::CHK: {
            auto condition = *cp->fp->ip++;
            auto position = *cp->fp->ip++;
            if (SRCLANG_VALUE_AS_BOOL(*(cp->sp - 1)) && condition == 1) {
                cp->fp->ip = (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                      ->fun->instructions->begin() +
                              position);
            } else if (!SRCLANG_VALUE_AS_BOOL(*(cp->sp - 1)) &&
                       condition == 0) {
                cp->fp->ip = (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                      ->fun->instructions->begin() +
                              position);
            }
        } break;

        case OpCode::JNZ: {
            auto value = *--(cp->sp);
            if (!SRCLANG_VALUE_AS_BOOL(value)) {
                cp->fp->ip = (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                      ->fun->instructions->begin() +
                              *cp->fp->ip);
            } else {
                *cp->fp->ip++;
            }
        } break;

        case OpCode::DEFER: {
            auto fn = *--(cp->sp);
            cp->fp->defers.push_back(fn);
        } break;

        case OpCode::CONTINUE:
        case OpCode::BREAK:
        case OpCode::JMP: {
            cp->fp->ip = (SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                                  ->fun->instructions->begin() +
                          *cp->fp->ip);
        } break;

        case OpCode::MODULE: {
            auto module_path = *--cp->sp;
            *cp->sp++ =
                    loadModule((wchar_t*)SRCLANG_VALUE_AS_STRING(module_path));
        } break;

        case OpCode::HLT: {
            for (auto i = cp->fp->defers.rbegin(); i != cp->fp->defers.rend();
                    i++)
                call(*i, {});

            return true;
        } break;

        default:
            error("unknown opcode '" +
                    SRCLANG_OPCODE_ID[static_cast<int>(inst)] + "'");
            return false;
        }
    }
}

Value Interpreter::run(const std::wstring& source, const std::wstring& filename,
        const std::vector<Value>& args) {
    Compiler compiler(source, filename, this, &symbolTable);
    Value code;
    try {
        code = compiler.compile();
    } catch (const std::wstring& exception) {
        return SRCLANG_VALUE_ERROR(wcsdup(exception.c_str()));
    }
    return call(code, args);
}

void Interpreter::push_context() {
    contexts.emplace_back();
    cp = contexts.begin() + contexts.size() - 1;
}

void Interpreter::pop_context() {
    contexts.pop_back();
    if (!contexts.empty())
        cp = contexts.begin() + contexts.size() - 1;
    else
        cp = contexts.end();
}

Value Interpreter::call(Value callee, const std::vector<Value>& args) {
    push_context();
    cp->fp->closure = callee;
    cp->fp->ip = SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)
                         ->fun->instructions->begin();
    *cp->sp++ = callee;
    for (auto const arg : args) { *cp->sp++ = arg; }
    cp->sp += SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->nlocals;
    cp->fp->bp = (cp->sp - args.size() -
                  SRCLANG_VALUE_AS_CLOSURE(cp->fp->closure)->fun->nlocals);

    Value result = SRCLANG_VALUE_NULL;
    if (run()) {
        if (cp->stack.begin() < cp->sp) { result = *(cp->sp - 1); }
    } else {
        result = SRCLANG_VALUE_ERROR(wcsdup(getError().c_str()));
    }
    pop_context();
    return result;
}

Value Interpreter::loadModule(std::wstring modulePath) {
    if (auto const itr = handlers.find(modulePath); itr != handlers.end()) {
        return itr->second.second;
    }

    try {
        modulePath = search(modulePath);
    } catch (const std::exception& exception) {
        return SRCLANG_VALUE_ERROR(wcsdup(s2ws(exception.what()).c_str()));
    }

    void* handler = dlopen(ws2s(modulePath).c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handler == nullptr) {
        return SRCLANG_VALUE_ERROR(wcsdup(s2ws(dlerror()).c_str()));
    }

    auto const map = new SrcLangMap();
    auto* fun = reinterpret_cast<void (*)(SrcLangMap*, Interpreter*)>(
            dlsym(handler, "srclang_module_init"));
    if (fun == nullptr) {
        return SRCLANG_VALUE_ERROR(wcsdup(s2ws(dlerror()).c_str()));
    }
    fun(map, this);

    return SRCLANG_VALUE_MAP(map);
}

std::wstring Interpreter::search(const std::wstring& id) {
    auto search_path = ":" + ws2s(getOption<std::wstring>(L"SEARCH_PATH"));
    std::stringstream ss(search_path);

    for (std::string p; getline(ss, p, ':');) {
        auto pt = std::filesystem::path(p) / ws2s(id);
        if (std::filesystem::exists(pt)) { return s2ws(pt.string()); }
        if (std::filesystem::exists(pt.string() + ".mod")) {
            return s2ws(pt.string() + ".mod");
        }
        if (std::filesystem::exists(pt.string() + ".src")) {
            return s2ws(pt.string() + ".src");
        }
    }
    throw std::runtime_error("missing required module '" + ws2s(id) + "'");
}
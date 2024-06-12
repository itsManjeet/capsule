#include "Interpreter.h"

#include <array>
#include <iostream>

#include "SymbolTable.h"
#include "Compiler.h"
#include "Builtin.h"

#include <dlfcn.h>


using namespace SrcLang;

void Interpreter::error(std::string const &msg) {
    if (debugInfo.empty() || debugInfo.back() == nullptr) {
        errStream << "ERROR: " << msg << std::endl;
        return;
    }

    errStream << debugInfo.back()->filename << ":"
              << debugInfo.back()->lines[distance(cp->fp->closure->fun->instructions->begin(), cp->fp->ip)]
              << std::endl;
    errStream << "  ERROR: " << msg;
}

Interpreter::Interpreter() :
        globals(65536),
        contexts(120) {

    cp = contexts.begin();
//    debug = true;

    for (auto b: builtins) {
        memoryManager.heap.push_back(b);
    }
    {
        int i = 0;
#define X(id) symbolTable.define(#id, i++);
        SRCLANG_BUILTIN_LIST
#undef X
    }
    auto sym = symbolTable.define("true");
    globals.at(sym.index) = SRCLANG_VALUE_BOOL(true);

    sym = symbolTable.define("false");
    globals.at(sym.index) = SRCLANG_VALUE_BOOL(false);

    sym = symbolTable.define("null");
    globals.at(sym.index) = SRCLANG_VALUE_NULL;
}

Interpreter::~Interpreter() = default;

void Interpreter::graceFullExit() {

}

void Interpreter::addObject(Value val) {
#ifdef SRCLANG_GC_DEBUG
    gc();
#else
    if (memoryManager.heap.size() > nextGc && nextGc < limitNextGc) {
        gc();
        memoryManager.heap.shrink_to_fit();
        nextGc = static_cast<int>(static_cast<float>(memoryManager.heap.size()) * gcHeapGrowFactor);
    }
#endif
    memoryManager.heap.push_back(val);
}

void Interpreter::gc() {
#ifdef SRCLANG_GC_DEBUG
    std::cout << "Total allocations: " << memoryManager.heap.size() << std::endl;
    std::cout << "gc begin:" << std::endl;
#endif
    for (auto &c: contexts) {
        memoryManager.mark(c.stack.begin(), c.sp);
    }
    memoryManager.mark(globals.begin(), globals.end());
    memoryManager.mark(constants.begin(), constants.end());
    memoryManager.mark(builtins.begin(), builtins.end());
    memoryManager.sweep();
#ifdef SRCLANG_GC_DEBUG
    std::cout << "gc end:" << std::endl;
    std::cout << "Total allocations: " << memoryManager.heap.size() << std::endl;
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
            char *aBuffer = (char *) SRCLANG_VALUE_AS_OBJECT(a)->pointer;
            char *bBuffer = (char *) SRCLANG_VALUE_AS_OBJECT(b)->pointer;
            return !strcmp(aBuffer, bBuffer);
        }

        case ValueType::List: {
            auto *aList = (SrcLangList *) SRCLANG_VALUE_AS_OBJECT(a)->pointer;
            auto *bList = (SrcLangList *) SRCLANG_VALUE_AS_OBJECT(b)->pointer;
            if (aList->size() != bList->size()) return false;
            for (int i = 0; i < aList->size(); i++) {
                if (!isEqual(aList->at(i), bList->at(i))) return false;
            }
            return true;
        }

        case ValueType::Map: {
            auto *aMap = (SrcLangMap *) SRCLANG_VALUE_AS_OBJECT(a)->pointer;
            auto *bMap = (SrcLangMap *) SRCLANG_VALUE_AS_OBJECT(b)->pointer;
            if (aMap->size() != bMap->size()) return false;
            for (auto const &i: *aMap) {
                auto iter = bMap->find(i.first);
                if (iter == bMap->end()) return false;
                if (!isEqual(iter->second, i.second)) return false;
            }
            return true;
        }
        default:
            return false;
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
                error("unexpected unary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(a) == ValueType::String) {
        switch (op) {
            default:
                error("unexpected unary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] + "'");
                return false;
        }
    } else {
        error("ERROR: unhandled unary operation for value of type " + SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                SRCLANG_VALUE_GET_TYPE(a))] + "'");
        return false;
    }

    return true;
}

bool Interpreter::binary(Value lhs, Value rhs, OpCode op) {
    if ((op == OpCode::NE || op == OpCode::EQ) && SRCLANG_VALUE_GET_TYPE(lhs) != SRCLANG_VALUE_GET_TYPE(rhs)) {
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
            error("can't apply binary operation '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] + "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_TYPE(rhs);
        switch (op) {
            case OpCode::EQ:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a == b);
                break;
            case OpCode::NE:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a != b);
                break;
            default:
                error("ERROR: unexpected binary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_NUMBER(lhs)) {
        auto a = SRCLANG_VALUE_AS_NUMBER(lhs);
        if (!SRCLANG_VALUE_IS_NUMBER(rhs)) {
            error("can't apply binary operation '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] + "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_NUMBER(rhs);
        switch (op) {
            case OpCode::ADD:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(a + b);
                break;
            case OpCode::SUB:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(a - b);
                break;
            case OpCode::MUL:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(a * b);
                break;
            case OpCode::DIV:
                *cp->sp++ = SRCLANG_VALUE_NUMBER(a / b);
                break;
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
                *cp->sp++ = SRCLANG_VALUE_NUMBER((long) a >> (long) b);
                break;
            case OpCode::RSHIFT:
                *cp->sp++ = SRCLANG_VALUE_NUMBER((long) a << (long) b);
                break;
            case OpCode::MOD:
                *cp->sp++ = SRCLANG_VALUE_NUMBER((long) a % (long) b);
                break;
            case OpCode::LOR:
                *cp->sp++ = SRCLANG_VALUE_NUMBER((long) a | (long) b);
                break;
            case OpCode::LAND:
                *cp->sp++ = SRCLANG_VALUE_NUMBER((long) a & (long) b);
                break;
            default:
                error("ERROR: unexpected binary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_BOOL(lhs)) {
        bool a = SRCLANG_VALUE_AS_BOOL(lhs);
        if (!SRCLANG_VALUE_IS_BOOL(rhs)) {
            error("can't apply binary operation '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] + "'");
            return false;
        }
        bool b = SRCLANG_VALUE_AS_BOOL(rhs);
        switch (op) {
            case OpCode::EQ:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a == b);
                break;
            case OpCode::NE:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a != b);
                break;
            case OpCode::AND:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a && b);
                break;
            case OpCode::OR:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a || b);
                break;
            default:
                error("ERROR: unexpected binary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::String) {
        char *a = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(lhs)->pointer);
        if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::String) {
            error("can't apply binary operation '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] + "'");
            return false;
        }
        char *b = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);

        switch (op) {
            case OpCode::ADD: {
                auto a_size = strlen(a);
                auto b_size = strlen(b);
                auto size = a_size + b_size + 1;

                char *buf = (char *) malloc(size);
                if (buf == nullptr) {
                    throw std::runtime_error("malloc() failed for string + string, " + std::string(strerror(errno)));
                }
                memcpy(buf, a, a_size);
                memcpy(buf + a_size, b, b_size);

                buf[size] = '\0';
                auto val = SRCLANG_VALUE_STRING(buf);
                addObject(val);
                *cp->sp++ = val;
            }
                break;
            case OpCode::EQ: {
                *cp->sp++ = strcmp(a, b) == 0 ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            }
                break;
            case OpCode::NE: {
                *cp->sp++ = strcmp(a, b) != 0 ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
            }
                break;
            case OpCode::GT:
                *cp->sp++ = SRCLANG_VALUE_BOOL(strlen(a) > strlen(b));
                break;
            case OpCode::LT:
                *cp->sp++ = SRCLANG_VALUE_BOOL(strlen(a) < strlen(b));
                break;
            default:
                error("ERROR: unexpected binary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::List) {
        auto a = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(lhs)->pointer);
        if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::List) {
            error("can't apply binary operation '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] + "'");
            return false;
        }

        auto b = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);
        switch (op) {
            case OpCode::ADD: {
                auto c = new SrcLangList(a->begin(), a->end());
                c->insert(c->end(), b->begin(), b->end());
                *cp->sp++ = SRCLANG_VALUE_LIST(c);
                addObject(*(cp->sp - 1));
            }
                break;
            case OpCode::GT:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a->size() > b->size());
                break;
            case OpCode::LT:
                *cp->sp++ = SRCLANG_VALUE_BOOL(a->size() < b->size());
                break;
            default:
                error("ERROR: unexpected binary operator '" + SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] + "'");
                return false;
        }
    } else {
        error("ERROR: unsupported binary operator '" + SRCLANG_OPCODE_ID[int(op)] + "' for type '" +
              SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                      SRCLANG_VALUE_GET_TYPE(lhs))] + "'");
        return false;
    }

    return true;
}

bool Interpreter::isFalsy(Value val) {
    return SRCLANG_VALUE_IS_NULL(val) || (SRCLANG_VALUE_IS_BOOL(val) && SRCLANG_VALUE_AS_BOOL(val) == false) ||
           (SRCLANG_VALUE_IS_NUMBER(val) && SRCLANG_VALUE_AS_NUMBER(val) == 0) ||
           (SRCLANG_VALUE_IS_OBJECT(val) && SRCLANG_VALUE_AS_OBJECT(val)->type == ValueType::Error);
}

void Interpreter::printStack() {
    if (debug) {
        std::cout << "  ";
        for (auto i = cp->stack.begin(); i != cp->sp; i++) {
            std::cout << "[" << SRCLANG_VALUE_GET_STRING(*i) << "] ";
        }
        std::cout << std::endl;
    }
}

bool Interpreter::callClosure(Value callee, uint8_t count) {
    auto closure = reinterpret_cast<Closure *>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    if (closure->fun->is_variadic) {
        if (count < closure->fun->nparams - 1) {
            error("expected atleast '" + std::to_string(closure->fun->nparams - 1) + "' but '" + std::to_string(count) +
                  "' provided");
            return false;
        }
        auto v_arg_begin = (cp->sp - (count - (closure->fun->nparams - 1)));
        auto v_arg_end = cp->sp;
        SrcLangList *var_args;
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
        error("expected '" + std::to_string(closure->fun->nparams) + "' but '" + std::to_string(count) + "' provided");
        return false;
    }

    cp->fp++;
    cp->fp->closure = closure;
    cp->fp->ip = cp->fp->closure->fun->instructions->begin();
    cp->fp->bp = (cp->sp - count);
    cp->sp = cp->fp->bp + cp->fp->closure->fun->nlocals;
    debugInfo.push_back(cp->fp->closure->fun->debug_info);

    return true;
}

bool Interpreter::callBuiltin(Value callee, uint8_t count) {
    auto builtin = reinterpret_cast<Builtin>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    std::vector<Value> args(cp->sp - count, cp->sp);
    cp->sp -= count + 1;
    Value result;
    try {
        result = builtin(args, this);
    } catch (std::exception const &exception) {
        error(exception.what());
        return false;
    }

    if (SRCLANG_VALUE_IS_OBJECT(result)) addObject(result);
    *cp->sp++ = result;
    return true;
}

bool Interpreter::callTypecastNum(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(cp->sp - count);
    cp->sp -= count + 1;
    if (SRCLANG_VALUE_IS_OBJECT(val) && SRCLANG_VALUE_AS_OBJECT(val)->type == ValueType::String) {
        try {
            *cp->sp++ = SRCLANG_VALUE_NUMBER(std::stod((char *) (SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
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
                result = SRCLANG_VALUE_STRING(
                        strdup(reinterpret_cast<const char *>(SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
            }
                break;

            case ValueType::List: {
                auto list = (SrcLangList *) SRCLANG_VALUE_AS_OBJECT(val)->pointer;
                std::string buf;
                for (auto i: *list) {
                    buf += SRCLANG_VALUE_GET_STRING(i);
                }
                result = SRCLANG_VALUE_STRING(strdup(buf.c_str()));
            }
                break;

            default:
                error("invalid type cast " + SRCLANG_VALUE_DEBUG(val));
                return false;
        }

    } else {
        std::string buf;
        for (auto i = cp->sp - count; i != cp->sp; i++) {
            buf += SRCLANG_VALUE_GET_STRING(*i);
        }
        result = SRCLANG_VALUE_STRING(strdup(buf.c_str()));
    }

    cp->sp -= count + 1;
    *cp->sp++ = result;
    return true;
}

bool Interpreter::callTypecastError(uint8_t count) {
    std::string buf;
    for (auto i = cp->sp - count; i != cp->sp; i++) {
        buf += SRCLANG_VALUE_GET_STRING(*i);
    }
    cp->sp -= count + 1;
    *cp->sp++ = SRCLANG_VALUE_ERROR(strdup(buf.c_str()));
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
        case ValueType::Number:
            return callTypecastNum(count);
        case ValueType::Type:
            return callTypecastType(count);
        case ValueType::Boolean:
            return callTypecastBool(count);
        case ValueType::String:
            return callTypecastString(count);
        case ValueType::Error:
            return callTypecastError(count);
            // case ValueType::Function:
            //     return call_typecast_function(count);
        default:
            error("invalid typecast");
            return false;
    }
}

bool Interpreter::callMap(Value callee, uint8_t count) {
    auto container = (SrcLangMap *) SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
    auto callback = container->find("__call__");
    if (callback == container->end()) {
        error("'__call__' is not defined in container");
        return false;
    }
    auto bounded_value = SRCLANG_VALUE_BOUNDED((new BoundedValue{callee, callback->second}));
    addObject(bounded_value);
    *(cp->sp - count - 1) = bounded_value;
    return callBounded(bounded_value, count);
}

bool Interpreter::callBounded(Value callee, uint8_t count) {
    auto bounded = (BoundedValue *) SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
    *(cp->sp - count - 1) = bounded->value;
    for (auto i = cp->sp; i != cp->sp - count; i--) {
        *i = *(i - 1);
    }
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
            case ValueType::Closure:
                return callClosure(callee, count);
            case ValueType::Builtin:
                return callBuiltin(callee, count);
            case ValueType::Bounded:
                return callBounded(callee, count);
            case ValueType::Map:
                return callMap(callee, count);
            default:
                error("ERROR: can't call object '" + SRCLANG_VALUE_DEBUG(callee) + "'");
                return false;
        }
    }
    error("ERROR: can't call value of type '" + SRCLANG_VALUE_DEBUG(callee) + "'");
    return false;
}

bool Interpreter::run() {
    while (true) {
        if (debug) {
            if (!debugInfo.empty() && debugInfo.back() != nullptr) {
                std::cout << debugInfo.back()->filename << ":"
                          << debugInfo.back()->lines[distance(cp->fp->closure->fun->instructions->begin(), cp->fp->ip)]
                          << std::endl;
            }

            std::cout << "  ";
            for (auto i = cp->stack.begin(); i != cp->sp; i++) {
                std::cout << "[" << SRCLANG_VALUE_DEBUG(*i) << "] ";
            }
            std::cout << std::endl;
            std::cout << ">> ";
            ByteCode::debug(*cp->fp->closure->fun->instructions, constants,
                            std::distance(cp->fp->closure->fun->instructions->begin(), cp->fp->ip), std::cout);
            std::cout << std::endl;

            if (break_) std::cin.get();
        }
        auto inst = static_cast<OpCode>(*cp->fp->ip++);
        switch (inst) {
            case OpCode::CONST_:
                *cp->sp++ = constants[*cp->fp->ip++];
                break;
            case OpCode::CONST_INT:
                *cp->sp++ = SRCLANG_VALUE_NUMBER((*cp->fp->ip++));
                break;
            case OpCode::CONST_FALSE:
                *cp->sp++ = SRCLANG_VALUE_FALSE;
                break;
            case OpCode::CONST_TRUE:
                *cp->sp++ = SRCLANG_VALUE_TRUE;
                break;
            case OpCode::CONST_NULL:
                *cp->sp++ = SRCLANG_VALUE_NULL;
                break;
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
                if (!binary(a, b, inst)) {
                    return false;
                }
            }
                break;

            case OpCode::COMMAND:
            case OpCode::NOT:
            case OpCode::NEG: {
                if (!unary(*--(cp->sp), inst)) {
                    return false;
                }
            }
                break;

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
                        error("Invalid STORE operation on '" + SRCLANG_SYMBOL_ID[int(scope)] + "'");
                        return false;
                }
            }
                break;

            case OpCode::LOAD: {
                auto scope = Symbol::Scope(*cp->fp->ip++);
                int pos = *cp->fp->ip++;
                switch (scope) {
                    case Symbol::Scope::LOCAL:
                        *cp->sp++ = *(cp->fp->bp + pos);
                        break;
                    case Symbol::Scope::GLOBAL:
                        *cp->sp++ = globals[pos];
                        break;
                    case Symbol::Scope::BUILTIN:
                        *cp->sp++ = builtins[pos];
                        break;
                    case Symbol::Scope::TYPE:
                        *cp->sp++ = SRCLANG_VALUE_TYPES[pos];
                        break;
                    case Symbol::Scope::FREE:
                        *cp->sp++ = cp->fp->closure->free[pos];
                        break;
                    default:
                        error("ERROR: can't load value of scope '" + SRCLANG_SYMBOL_ID[int(scope)] + "'");
                        return false;
                }

            }
                break;

            case OpCode::CLOSURE: {
                int funIndex = *cp->fp->ip++;
                int nfree = *cp->fp->ip++;

                auto constant = constants[funIndex];
                auto fun = (Function *) SRCLANG_VALUE_AS_OBJECT(constant)->pointer;
                auto frees = std::vector<Value>(cp->sp - nfree, cp->sp);
                cp->sp -= nfree;
                auto closure = new Closure{fun, frees};
                auto closure_value = SRCLANG_VALUE_CLOSURE(closure);
                *cp->sp++ = closure_value;
                addObject(closure_value);
            }
                break;

            case OpCode::CALL: {
                int count = *cp->fp->ip++;
                if (!call(count)) {
                    return false;
                }
            }
                break;

            case OpCode::POP: {
                if (cp->sp == cp->stack.begin()) {
                    error("Stack-underflow");
                    return false;
                }
                *--(cp->sp);
            }
                break;

            case OpCode::PACK: {
                auto size = *cp->fp->ip++;
                auto list = new std::vector<Value>(cp->sp - size, cp->sp);
                cp->sp -= size;
                auto list_value = SRCLANG_VALUE_LIST(list);
                addObject(list_value);
                *cp->sp++ = list_value;
            }
                break;

            case OpCode::MAP: {
                auto size = *cp->fp->ip++;
                auto map = new SrcLangMap();
                for (auto i = cp->sp - (size * 2); i != cp->sp; i += 2) {
                    map->insert({(char *) SRCLANG_VALUE_AS_OBJECT(*(i))->pointer, *(i + 1)});
                }
                cp->sp -= (size * 2);
                *cp->sp++ = SRCLANG_VALUE_MAP(map);
                addObject(*(cp->sp - 1));
            }
                break;

            case OpCode::INDEX: {
                auto count = *cp->fp->ip++;
                Value pos, end_idx;
                switch (count) {
                    case 1:
                        pos = *--(cp->sp);
                        break;
                    case 2:
                        end_idx = *--(cp->sp);
                        pos = *--(cp->sp);
                        if (!(SRCLANG_VALUE_IS_NUMBER(pos) && SRCLANG_VALUE_IS_NUMBER(end_idx))) {
                            error("invalid INDEX for range");
                            return false;
                        }
                        break;
                    default:
                        error("invalid INDEX count");
                        return false;
                }
                auto container = *--(cp->sp);
                if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                    char *buffer = (char *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                    int index = SRCLANG_VALUE_AS_NUMBER(pos);
                    int len = strlen(buffer);
                    switch (count) {
                        case 1: {
                            if (strlen(buffer) <= index || index < 0) {
                                *cp->sp++ = SRCLANG_VALUE_NULL;
                            } else {
                                *cp->sp++ = SRCLANG_VALUE_STRING(strdup(std::string(1, buffer[index]).c_str()));
                            }

                        }
                            break;
                        case 2: {
                            int end = SRCLANG_VALUE_AS_NUMBER(end_idx);
                            if (index < 0) index = len + index;
                            if (end < 0) end = len + end + 1;
                            if (end - index < 0 || end - index >= len) {
                                *cp->sp++ = SRCLANG_VALUE_NULL;
                            } else {
                                char *buf = (char *) malloc(sizeof(char) * (end - index + 1));
                                memcpy(buf, buffer + index, end - index);

                                buf[end - index] = '\0';
                                *cp->sp++ = SRCLANG_VALUE_STRING(buf);

                                addObject(*(cp->sp - 1));
                            }
                        }
                            break;
                        default:
                            error("Invalid INDEX range operation for string");
                            return false;
                    }

                } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::List &&
                           SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                    std::vector<Value> list = *(std::vector<Value> *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;

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

                        }
                            break;
                        default:
                            error("Invalid INDEX range operation for list");
                            return false;
                    }

                } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::Map &&
                           SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                    auto map = *reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    auto buf = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                    auto get_index_callback = map.find("__index__");
                    if (get_index_callback == map.end()) {
                        auto idx = map.find(buf);
                        if (idx == map.end()) {
                            *cp->sp++ = SRCLANG_VALUE_NULL;
                        } else {
                            *cp->sp++ = idx->second;
                        }
                    } else {
                        auto bounded_value = SRCLANG_VALUE_BOUNDED(
                                (new BoundedValue{container, get_index_callback->second}));
                        addObject(bounded_value);

                        *cp->sp++ = bounded_value;
                        *cp->sp++ = pos;
                        if (!callBounded(bounded_value, 1)) {
                            return false;
                        }
                    }

                } else {
                    error("InvalidOperation b/w '" + SRCLANG_VALUE_DEBUG(pos) + "' and '" +
                          SRCLANG_VALUE_DEBUG(container) + "'");
                    return false;
                }
            }
                break;

            case OpCode::SET_SELF: {
                auto freeIndex = *cp->fp->ip++;

                auto currentClosure = cp->fp->closure;
                currentClosure->free[freeIndex] = memoryManager.addObject(SRCLANG_VALUE_CLOSURE(currentClosure));
                SRCLANG_VALUE_SET_REF(currentClosure->free[freeIndex]);
            }
                break;

            case OpCode::SET: {
                auto val = *--(cp->sp);
                auto pos = *--(cp->sp);
                auto container = *--(cp->sp);
                if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                    auto idx = SRCLANG_VALUE_AS_NUMBER(pos);
                    char *buf = (char *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                    int size = strlen(buf);
                    if (idx < 0 || size <= idx) {
                        error("out of bound");
                        return false;
                    }
                    if (SRCLANG_VALUE_IS_NUMBER(val)) {
                        buf = (char *) realloc(buf, size);
                        if (buf == nullptr) {
                            error("out of memory");
                            return false;
                        }
                        buf[long(idx)] = (char) SRCLANG_VALUE_AS_NUMBER(val);
                    } else if (SRCLANG_VALUE_GET_TYPE(val) == ValueType::String) {
                        char *b = (char *) SRCLANG_VALUE_AS_OBJECT(val)->pointer;
                        size_t b_size = strlen(b);
                        buf = (char *) realloc(buf, size + b_size);
                        strcat(buf, b);
                    } else {
                        error("can't SET '" + SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(val))] + "' to string");
                        return true;
                    }
                } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::List &&
                           SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                    auto list = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    list->at(SRCLANG_VALUE_AS_NUMBER(pos)) = val;
                } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::Map &&
                           SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                    auto map = reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    auto buf = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                    auto set_index_callback = map->find("__set_index__");
                    if (set_index_callback == map->end()) {
                        if (map->find(buf) == map->end()) {
                            map->insert({buf, val});
                        } else {
                            map->at(buf) = val;
                        }
                    } else {
                        auto bounded_value = SRCLANG_VALUE_BOUNDED(
                                (new BoundedValue{container, set_index_callback->second}));
                        addObject(bounded_value);

                        *cp->sp++ = bounded_value;
                        *cp->sp++ = pos;
                        *cp->sp++ = val;
                        if (!callBounded(bounded_value, 2)) {
                            return false;
                        }
                    }

                } else if (SRCLANG_VALUE_GET_TYPE(container) == ValueType::Pointer &&
                           SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Number) {
                    auto idx = SRCLANG_VALUE_AS_NUMBER(pos);
                    char *buf = (char *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                    char value = SRCLANG_VALUE_AS_NUMBER(val);
                    buf[long(idx)] = value;
                } else {
                    error("invalid SET operation for '" +
                          SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(container))] + "' and '" +
                          SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(pos))] + "'");
                    return false;
                }
                *cp->sp++ = container;
            }
                break;

            case OpCode::SIZE: {
                auto val = *--(cp->sp);
                if (!SRCLANG_VALUE_IS_OBJECT(val)) {
                    error("container in loop is not iterable");
                    return false;
                }
                auto obj = SRCLANG_VALUE_AS_OBJECT(val);
                switch (obj->type) {
                    case ValueType::String:
                        *cp->sp++ = SRCLANG_VALUE_NUMBER(strlen((char *) obj->pointer));
                        break;
                    case ValueType::List:
                        *cp->sp++ = SRCLANG_VALUE_NUMBER(((SrcLangList *) obj->pointer)->size());
                        break;
                    default:
                        error("container '" + SRCLANG_VALUE_GET_STRING(val) + "' is not a iterable object");
                        return false;
                }
            }
                break;

            case OpCode::RET: {
                auto value = *--(cp->sp);
                for (auto i = cp->fp->defers.rbegin(); i != cp->fp->defers.rend(); i++)
                    call(*i, {});

                cp->sp = cp->fp->bp - 1;
                cp->fp--;
                *cp->sp++ = value;
                debugInfo.pop_back();
            }
                break;

            case OpCode::CHK: {
                auto condition = *cp->fp->ip++;
                auto position = *cp->fp->ip++;
                if (SRCLANG_VALUE_AS_BOOL(*(cp->sp - 1)) && condition == 1) {
                    cp->fp->ip = (cp->fp->closure->fun->instructions->begin() + position);
                } else if (!SRCLANG_VALUE_AS_BOOL(*(cp->sp - 1)) && condition == 0) {
                    cp->fp->ip = (cp->fp->closure->fun->instructions->begin() + position);
                }
            }
                break;

            case OpCode::JNZ: {
                auto value = *--(cp->sp);
                if (!SRCLANG_VALUE_AS_BOOL(value)) {
                    cp->fp->ip = (cp->fp->closure->fun->instructions->begin() + *cp->fp->ip);
                } else {
                    *cp->fp->ip++;
                }
            }
                break;

            case OpCode::DEFER: {
                auto fn = *--(cp->sp);
                cp->fp->defers.push_back(fn);
            }
                break;

            case OpCode::CONTINUE:
            case OpCode::BREAK:
            case OpCode::JMP: {
                cp->fp->ip = (cp->fp->closure->fun->instructions->begin() + *cp->fp->ip);
            }
                break;

            case OpCode::MODULE: {
                auto module_path = *--cp->sp;
                *cp->sp++ = loadModule((const char *) SRCLANG_VALUE_AS_OBJECT(module_path)->pointer);
            }
                break;

            case OpCode::HLT: {
                for (auto i = cp->fp->defers.rbegin(); i != cp->fp->defers.rend(); i++)
                    call(*i, {});

                return true;
            }
                break;

            default:
                error("unknown opcode '" + SRCLANG_OPCODE_ID[static_cast<int>(inst)] + "'");
                return false;
        }
    }
}

Value Interpreter::run(const std::string &source, const std::string &filename) {
    Compiler compiler(source, filename, constants, &symbolTable, &memoryManager);
    try {
        compiler.compile();
    } catch (const std::exception &exception) {
        return SRCLANG_VALUE_ERROR(strdup(exception.what()));
    }

    auto code = compiler.code();

    return run(code, compiler.debugInfo());
}

void Interpreter::push_context() {
    contexts.emplace_back();
    cp = contexts.begin() + contexts.size() - 1;
}

void Interpreter::pop_context() {
    contexts.pop_back();
    cp = contexts.begin() + contexts.size() - 1;
}

Value Interpreter::call(Value callee, const std::vector<Value> &args) {
    ByteCode code;
    code.instructions = std::make_unique<Instructions>();
    code.constants = this->constants;

    code.constants.push_back(callee);
    code.instructions->push_back(static_cast<const unsigned int>(OpCode::CONST_));
    code.instructions->push_back(code.constants.size() - 1);

    for (auto arg: args) {
        code.constants.push_back(arg);
        code.instructions->push_back(static_cast<const unsigned int>(OpCode::CONST_));
        code.instructions->push_back(code.constants.size() - 1);
    }

    code.instructions->push_back(static_cast<const unsigned int>(OpCode::CALL));
    code.instructions->push_back(args.size());
    code.instructions->push_back(static_cast<const unsigned int>(OpCode::HLT));

    this->constants = code.constants;

    return run(code, nullptr);
}

Value Interpreter::run(ByteCode &bytecode, const std::shared_ptr<DebugInfo> &debug_info) {
    debugInfo.push_back(debug_info);
    auto fun = new Function{FunctionType::Function,
                            "<script>",
                            std::move(bytecode.instructions),
                            0,
                            0,
                            false,
                            debug_info};


    push_context();
    cp->fp->closure = new Closure{fun, {}};
    cp->fp->ip = cp->fp->closure->fun->instructions->begin();
    cp->fp->bp = cp->sp;

    Value result = SRCLANG_VALUE_NULL;
    if (run()) {
        if (std::distance(cp->stack.begin(), cp->sp) > 0) {
            result = *(cp->sp - 1);
        }
    } else {
        result = SRCLANG_VALUE_ERROR(strdup(getError().c_str()));
    }

    pop_context();
    return result;
}

Value Interpreter::loadModule(const std::filesystem::path &modulePath) {
    auto itr = handlers.find(modulePath.string());
    if (itr != handlers.end()) {
        return itr->second.second;
    }

    std::stringstream ss(".:" +
                         std::string(getenv("SRCLANG_SEARCH_PATH") ? getenv("SRCLANG_SEARCH_PATH")
                                                                   : "/usr/lib/SrcLang"));
    std::string search_path;
    bool found = false;
    while (getline(ss, search_path, ':')) {
        search_path += "/" + modulePath.string();
        if (std::filesystem::exists(search_path)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return SRCLANG_VALUE_ERROR(strdup(("missing required module '" + modulePath.string() + "'").c_str()));
    }

    void *handler = dlopen(search_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handler == nullptr) {
        return SRCLANG_VALUE_ERROR(strdup(dlerror()));
    }

    auto map = new SrcLangMap();
    auto *fun = (void (*)(SrcLangMap *, Interpreter *)) dlsym(handler, "srclang_module_init");
    if (fun == nullptr) {
        return SRCLANG_VALUE_ERROR(strdup(dlerror()));
    }
    fun(map, this);

    return SRCLANG_VALUE_MAP(map);
}

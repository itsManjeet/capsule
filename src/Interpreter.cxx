#include <array>
#include <ranges>
#include "Interpreter.hxx"
#include "SymbolTable.hxx"
#include "Language.hxx"

using namespace srclang;

#include <ffi.h>
#include <dlfcn.h>

void Interpreter::error(std::string const &mesg) {
    if (debug_info.empty() || debug_info.back() == nullptr) {
        err_stream << "ERROR: " << mesg << std::endl;
        return;
    }

    err_stream << debug_info.back()->filename << ":"
               << debug_info.back()->lines[distance(
                       cur()->closure->fun->instructions->begin(), cur()->ip)]
               << std::endl;
    err_stream << "  ERROR: " << mesg;
}

Interpreter::Interpreter(ByteCode &code, const std::shared_ptr<DebugInfo> &debugInfo, Language *language)
        : stack(2048),
          frames(1024),
          language{language} {

    GC_HEAP_GROW_FACTOR =
            std::get<float>(language->options.at("GC_HEAP_GROW_FACTOR"));
    next_gc =
            std::get<int>(language->options.at("GC_INITIAL_TRIGGER"));
    sp = stack.begin();
    fp = frames.begin();
    debug_info.push_back(debugInfo);
    auto fun = new Function{FunctionType::Function, "<script>",
                            std::move(code.instructions),
                            0,
                            0,
                            false,
                            debugInfo};

    debug = get<bool>(language->options.at("DEBUG"));
    break_ = get<bool>(language->options.at("BREAK"));

    fp->closure = new Closure{std::move(fun), {}};
    fp->ip = fp->closure->fun->instructions->begin();
    fp->bp = sp;
    fp++;
}

Interpreter::~Interpreter() {
    delete (frames.begin())->closure->fun;
    delete (frames.begin())->closure;
}

void Interpreter::add_object(Value val) {
#ifdef SRCLANG_GC_DEBUG
    gc();
#else
    if (language->memoryManager.heap.size() > next_gc && next_gc < LIMIT_NEXT_GC) {
        std::cout << "TRIGGERING GC:" << std::endl;
        gc();
        language->memoryManager.heap.shrink_to_fit();
        next_gc = language->memoryManager.heap.size() * GC_HEAP_GROW_FACTOR;
        std::cout << "NEXT GC:" << next_gc << std::endl;
    }
#endif
    language->memoryManager.heap.push_back(val);
}


void Interpreter::gc() {
#ifdef SRCLANG_GC_DEBUG
    cout << "Total allocations: " << language->memoryManager.heap.size() << endl;
        cout << "gc begin:" << endl;
#endif
    language->memoryManager.mark(stack.begin(), sp);
    language->memoryManager.mark(language->globals.begin(), language->globals.end());
    language->memoryManager.mark(language->constants.begin(), language->constants.end());
    language->memoryManager.mark(builtins.begin(), builtins.end());
    language->memoryManager.sweep();
#ifdef SRCLANG_GC_DEBUG
    cout << "gc end:" << endl;
        cout << "Total allocations: " << language->memoryManager.heap.size() << endl;
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
            SrcLangList *aList = (SrcLangList *) SRCLANG_VALUE_AS_OBJECT(a)->pointer;
            SrcLangList *bList = (SrcLangList *) SRCLANG_VALUE_AS_OBJECT(b)->pointer;
            if (aList->size() != bList->size()) return false;
            for (int i = 0; i < aList->size(); i++) {
                if (!isEqual(aList->at(i), bList->at(i))) return false;
            }
            return true;
        }

        case ValueType::Map: {
            SrcLangMap *aMap = (SrcLangMap *) SRCLANG_VALUE_AS_OBJECT(a)->pointer;
            SrcLangMap *bMap = (SrcLangMap *) SRCLANG_VALUE_AS_OBJECT(b)->pointer;
            if (aMap->size() != bMap->size()) return false;
            for (auto const &i: *aMap) {
                auto iter = bMap->find(i.first);
                if (iter == bMap->end()) return false;
                if (!isEqual(iter->second, i.second)) return false;
            }
            return true;
        }
    }
    return false;
}

bool Interpreter::unary(Value a, OpCode op) {
    if (OpCode::NOT == op) {
        *sp++ = SRCLANG_VALUE_BOOL(is_falsy(a));
        return true;
    }
    if (SRCLANG_VALUE_IS_INTEGER(a)) {
        switch (op) {
            case OpCode::NEG:
                *sp++ = SRCLANG_VALUE_INTEGER(-SRCLANG_VALUE_AS_INTEGER(a));
                break;
            default:
                error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] +
                        "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_DECIMAL(a)) {
        switch (op) {
            case OpCode::NEG:
                *sp++ = SRCLANG_VALUE_DECIMAL(-SRCLANG_VALUE_AS_DECIMAL(a));
                break;
            default:
                error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] +
                        "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(a) == ValueType::String) {
        switch (op) {
            case OpCode::COMMAND: {
                std::array<char, 128> buffer;
                std::string result;
                std::unique_ptr<FILE, decltype(&pclose)> pipe(
                        popen((char *) (SRCLANG_VALUE_AS_OBJECT(a))->pointer,
                              "r"),
                        pclose);
                if (!pipe) {
                    throw std::runtime_error("popen() failed!");
                }
                while (fgets(buffer.data(), buffer.size(), pipe.get()) !=
                       nullptr) {
                    result += buffer.data();
                }
                auto string_value =
                        SRCLANG_VALUE_STRING(strdup(result.c_str()));
                add_object(string_value);
                *sp++ = string_value;
            }
                break;
            default:
                error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(a))] +
                        "'");
                return false;
        }
    } else {
        error("ERROR: unhandler unary operation for value of type " +
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
        *sp++ = op == OpCode::NE ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
        return true;
    }

    if (SRCLANG_VALUE_IS_NULL(lhs) && SRCLANG_VALUE_IS_NULL(rhs)) {
        *sp++ = op == OpCode::EQ ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
        return true;
    }

    if (SRCLANG_VALUE_IS_CHAR(lhs)) {
        auto a = SRCLANG_VALUE_AS_CHAR(lhs);
        if (!SRCLANG_VALUE_IS_CHAR(rhs)) {
            error("can't apply binary operation '" +
                  SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                  "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                  "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_CHAR(rhs);
        switch (op) {
            case OpCode::ADD: {
                char *buf = new char[3];
                buf[0] = a;
                buf[1] = b;
                buf[2] = '\0';
                *sp++ = SRCLANG_VALUE_STRING(buf);
                add_object(*(sp - 1));
            }
                break;
            case OpCode::EQ:
                *sp++ = a == b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::NE:
                *sp++ = a != b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_TYPE(lhs)) {
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
            case OpCode::EQ:
                *sp++ = SRCLANG_VALUE_BOOL(a == b);
                break;
            case OpCode::NE:
                *sp++ = SRCLANG_VALUE_BOOL(a != b);
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_INTEGER(lhs)) {
        auto a = SRCLANG_VALUE_AS_INTEGER(lhs);
        if (!SRCLANG_VALUE_IS_INTEGER(rhs)) {
            error("can't apply binary operation '" +
                  SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                  "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                  "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_INTEGER(rhs);
        switch (op) {
            case OpCode::ADD:
                *sp++ = SRCLANG_VALUE_INTEGER(a + b);
                break;
            case OpCode::SUB:
                *sp++ = SRCLANG_VALUE_INTEGER(a - b);
                break;
            case OpCode::MUL:
                *sp++ = SRCLANG_VALUE_INTEGER(a * b);
                break;
            case OpCode::DIV:
                *sp++ = SRCLANG_VALUE_INTEGER(a / b);
                break;
            case OpCode::EQ:
                *sp++ = a == b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::NE:
                *sp++ = a != b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::LT:
                *sp++ = a < b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::LE:
                *sp++ = a <= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::GT:
                *sp++ = a > b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::GE:
                *sp++ = a >= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::LSHIFT:
                *sp++ = SRCLANG_VALUE_INTEGER(a >> b);
                break;
            case OpCode::RSHIFT:
                *sp++ = SRCLANG_VALUE_INTEGER(a << b);
                break;
            case OpCode::MOD:
                *sp++ = SRCLANG_VALUE_INTEGER(a % b);
                break;
            case OpCode::LOR:
                *sp++ = SRCLANG_VALUE_INTEGER(a | b);
                break;
            case OpCode::LAND:
                *sp++ = SRCLANG_VALUE_INTEGER(a & b);
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
            case OpCode::EQ:
                *sp++ = SRCLANG_VALUE_BOOL(a == b);
                break;
            case OpCode::NE:
                *sp++ = SRCLANG_VALUE_BOOL(a != b);
                break;
            case OpCode::AND:
                *sp++ = SRCLANG_VALUE_BOOL(a && b);
                break;
            case OpCode::OR:
                *sp++ = SRCLANG_VALUE_BOOL(a || b);
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_IS_DECIMAL(lhs)) {
        auto a = SRCLANG_VALUE_AS_DECIMAL(lhs);
        if (!SRCLANG_VALUE_IS_DECIMAL(rhs)) {
            error("can't apply binary operation '" +
                  SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                  "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                  "'");
            return false;
        }
        auto b = SRCLANG_VALUE_AS_DECIMAL(rhs);
        switch (op) {
            case OpCode::ADD:
                *sp++ = SRCLANG_VALUE_DECIMAL(a + b);
                break;
            case OpCode::SUB:
                *sp++ = SRCLANG_VALUE_DECIMAL(a - b);
                break;
            case OpCode::MUL:
                *sp++ = SRCLANG_VALUE_DECIMAL(a * b);
                break;
            case OpCode::DIV:
                *sp++ = SRCLANG_VALUE_DECIMAL(a / b);
                break;
            case OpCode::EQ:
                *sp++ = a == b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::NE:
                *sp++ = a != b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::LT:
                *sp++ = a < b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::LE:
                *sp++ = a <= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::GT:
                *sp++ = a > b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            case OpCode::GE:
                *sp++ = a >= b ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE;
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::String) {
        char *a =
                reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(lhs)->pointer);
        if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::String) {
            error("can't apply binary operation '" +
                  SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                  "' and '" +
                  SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                  "'");
            return false;
        }
        char *b =
                reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);

        switch (op) {
            case OpCode::ADD: {
                int size = strlen(a) + strlen(b) + 1;
                char *buf = new char[size];
                snprintf(buf, size, "%s%s", a, b);
                buf[size] = '\0';
                auto val = SRCLANG_VALUE_STRING(buf);
                add_object(val);
                *sp++ = val;
            }
                break;
            case OpCode::EQ: {
                *sp++ = strcmp(a, b) == 0 ? SRCLANG_VALUE_TRUE
                                          : SRCLANG_VALUE_FALSE;
            }
                break;
            case OpCode::NE: {
                *sp++ = strcmp(a, b) != 0 ? SRCLANG_VALUE_TRUE
                                          : SRCLANG_VALUE_FALSE;
            }
                break;
            case OpCode::GT:
                *sp++ = SRCLANG_VALUE_BOOL(strlen(a) > strlen(b));
                break;
            case OpCode::LT:
                *sp++ = SRCLANG_VALUE_BOOL(strlen(a) < strlen(b));
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(
                              SRCLANG_VALUE_GET_TYPE(lhs))] +
                      "'");
                return false;
        }
    } else if (SRCLANG_VALUE_GET_TYPE(lhs) == ValueType::List) {
        auto a = reinterpret_cast<SrcLangList *>(
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

        auto b = reinterpret_cast<SrcLangList *>(
                SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);
        switch (op) {
            case OpCode::ADD: {
                auto c = new SrcLangList(a->begin(), a->end());
                c->insert(c->end(), b->begin(), b->end());
                *sp++ = SRCLANG_VALUE_LIST(c);
                add_object(*(sp - 1));
            }
                break;
            case OpCode::GT:
                *sp++ = SRCLANG_VALUE_BOOL(a->size() > b->size());
                break;
            case OpCode::LT:
                *sp++ = SRCLANG_VALUE_BOOL(a->size() < b->size());
                break;
            default:
                error("ERROR: unexpected binary operator '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(
                              SRCLANG_VALUE_GET_TYPE(lhs))] +
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

bool Interpreter::is_falsy(Value val) {
    return SRCLANG_VALUE_IS_NULL(val) ||
           (SRCLANG_VALUE_IS_BOOL(val) &&
            SRCLANG_VALUE_AS_BOOL(val) == false) ||
           (SRCLANG_VALUE_IS_INTEGER(val) &&
            SRCLANG_VALUE_AS_INTEGER(val) == 0);
}

void Interpreter::print_stack() {
    if (debug) {
        std::cout << "  ";
        for (auto i = stack.begin(); i != sp; i++) {
            std::cout << "[" << SRCLANG_VALUE_GET_STRING(*i) << "] ";
        }
        std::cout << std::endl;
    }
}

bool Interpreter::call_closure(Value callee, uint8_t count) {
    auto closure = reinterpret_cast<Closure *>(
            SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    if (closure->fun->is_variadic) {
        if (count < closure->fun->nparams - 1) {
            error("expected atleast '" + std::to_string(closure->fun->nparams - 1) + "' but '" + std::to_string(count) +
                  "' provided");
            return false;
        }
        auto v_arg_begin = (sp - (count - (closure->fun->nparams - 1)));
        auto v_arg_end = sp;
        SrcLangList *var_args;
        auto dist = distance(v_arg_begin, v_arg_end);
        if (dist == 0) {
            var_args = new SrcLangList();
        } else {
            var_args = new SrcLangList(v_arg_begin, v_arg_end);
        }
        auto var_val = SRCLANG_VALUE_LIST(var_args);
        add_object(var_val);
        *(sp - (count - (closure->fun->nparams - 1))) = var_val;
        sp = (sp - (count - closure->fun->nparams));
        count = closure->fun->nparams;

        print_stack();
    }

    if (count != closure->fun->nparams) {
        error("expected '" + std::to_string(closure->fun->nparams) + "' but '" + std::to_string(count) + "' provided");
        return false;
    }


    fp->closure = closure;
    fp->ip = fp->closure->fun->instructions->begin();
    fp->bp = (sp - count);
    sp = fp->bp + fp->closure->fun->nlocals;
    print_stack();
    debug_info.push_back(fp->closure->fun->debug_info);
    fp++;
    return true;
}

bool Interpreter::call_builtin(Value callee, uint8_t count) {
    auto builtin =
            reinterpret_cast<Builtin>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    std::vector<Value> args(sp - count, sp);
    sp -= count + 1;
    Value result;
    try {
        result = builtin(args, this);
    } catch (std::exception const &exception) {
        error(exception.what());
        return false;
    }

    if (SRCLANG_VALUE_IS_OBJECT(result)) add_object(result);
    *sp++ = result;
    return true;
}

bool Interpreter::call_typecast_int(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(sp - count);
    sp -= count + 1;
    if (SRCLANG_VALUE_IS_OBJECT(val) &&
        SRCLANG_VALUE_AS_OBJECT(val)->type == ValueType::String) {
        try {
            *sp++ = SRCLANG_VALUE_INTEGER(
                    std::stoi((char *) (SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
        } catch (...) {
            error("invalid typecast");
            return false;
        }
    } else if (SRCLANG_VALUE_IS_CHAR(val)) {
        *sp++ = SRCLANG_VALUE_INTEGER(int(SRCLANG_VALUE_AS_CHAR(val)));
    } else if (SRCLANG_VALUE_IS_DECIMAL(val)) {
        *sp++ = SRCLANG_VALUE_INTEGER(int(SRCLANG_VALUE_AS_DECIMAL(val)));
    }
    return true;
}

bool Interpreter::call_typecast_float(uint8_t count) {
    if (count != 1 || !SRCLANG_VALUE_IS_OBJECT(*(sp - count)) ||
        SRCLANG_VALUE_AS_OBJECT(*(sp - count))->type != ValueType::String) {
        error("invalid typecast");
        return false;
    }
    Value val = *(sp - count);
    sp -= count + 1;
    try {
        *sp++ = SRCLANG_VALUE_DECIMAL(
                std::stod((char *) (SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
    } catch (...) {
        error("invalid typecast");
        return false;
    }
    return true;
}

bool Interpreter::call_typecast_char(uint8_t count) {
    if (count != 1 || !SRCLANG_VALUE_IS_INTEGER(*(sp - count))) {
        error("invalid typecast");
        return false;
    }
    Value val = *(sp - count);
    sp -= count + 1;
    try {
        *sp++ = SRCLANG_VALUE_CHAR(SRCLANG_VALUE_AS_INTEGER(val));
    } catch (...) {
        error("invalid typecast");
        return false;
    }
    return true;
}

bool Interpreter::call_typecast_string(uint8_t count) {
    Value result;
    if (count == 1) {
        auto value = *(sp - count);
        switch (SRCLANG_VALUE_GET_TYPE(value)) {
            case ValueType::Char:
                result = SRCLANG_VALUE_STRING(strdup(std::string(1, SRCLANG_VALUE_AS_CHAR(value)).c_str()));
                break;
            case ValueType::Pointer:
                result = SRCLANG_VALUE_STRING(SRCLANG_VALUE_AS_OBJECT(value)->pointer);
                break;
            default:
                result = SRCLANG_VALUE_STRING(strdup(SRCLANG_VALUE_GET_STRING(value).c_str()));
                break;
        }
    } else {
        std::string buf;
        for (auto i = sp - count; i != sp; i++) {
            buf += SRCLANG_VALUE_GET_STRING(*i);
        }
        result = SRCLANG_VALUE_STRING(strdup(buf.c_str()));
    }

    sp -= count + 1;
    *sp++ = result;
    return true;
}

bool Interpreter::call_typecast_error(uint8_t count) {
    std::string buf;
    for (auto i = sp - count; i != sp; i++) {
        buf += SRCLANG_VALUE_GET_STRING(*i);
    }
    sp -= count + 1;
    *sp++ = SRCLANG_VALUE_ERROR(strdup(buf.c_str()));
    return true;
}

bool Interpreter::call_typecast_bool(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(sp - count);
    sp -= count + 1;
    *sp++ = SRCLANG_VALUE_BOOL(!is_falsy(val));
    return true;
}

bool Interpreter::call_typecast_type(uint8_t count) {
    if (count != 1) {
        error("invalid typecast");
        return false;
    }
    Value val = *(sp - count);
    sp -= count + 1;
    *sp++ = SRCLANG_VALUE_TYPE(SRCLANG_VALUE_GET_TYPE(val));
    return true;
}

bool Interpreter::call_typecast(Value callee, uint8_t count) {
    auto type = SRCLANG_VALUE_AS_TYPE(callee);
    switch (type) {
        case ValueType::Integer:
            return call_typecast_int(count);
        case ValueType::Decimal:
            return call_typecast_float(count);
        case ValueType::Char:
            return call_typecast_char(count);
        case ValueType::Type:
            return call_typecast_type(count);
        case ValueType::Boolean:
            return call_typecast_bool(count);
        case ValueType::String:
            return call_typecast_string(count);
        case ValueType::Error:
            return call_typecast_error(count);
            // case ValueType::Function:
            //     return call_typecast_function(count);
        default:
            error("invalid typecast");
            return false;
    }
}

bool Interpreter::call_native(Value callee, uint8_t count) {
    auto native = reinterpret_cast<NativeFunction *>(
            SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
    if (native->param.size() != count) {
        error("Expected '" + std::to_string(native->param.size()) + "' but '" +
              std::to_string(count) + "' provided");
        return false;
    }

    void *handler = nullptr;
    handler = dlsym(nullptr, native->id.c_str());
    if (handler == nullptr) {
        error(dlerror());
        return false;
    }

    ffi_cif cif;
    void *values[count];
    ffi_type *types[count];
    int j = 0;
    for (auto i = sp - count; i != sp; i++, j++) {
        auto type = SRCLANG_VALUE_GET_TYPE(*i);
        if (type != native->param[j]) {
            error("ERROR: invalid " + std::to_string(j) + "th parameter, expected '" +
                  SRCLANG_VALUE_TYPE_ID[int(native->param[j])] + "' but got '" +
                  SRCLANG_VALUE_TYPE_ID[int(type)] +
                  "'");
            return false;
        }
        switch (type) {
            case ValueType::Null:
                values[j] = nullptr;
                types[j] = &ffi_type_pointer;
                break;
            case ValueType::Integer: {
                values[j] = &(*i);
                (*(int64_t *) (values[j])) >>= 3;
                types[j] = &ffi_type_slong;

            }
                break;
            case ValueType::Char: {
                values[j] = &(*i);
                (*(char *) (values[j])) >>= 3;
                types[j] = &ffi_type_uint8;
            }
                break;
            case ValueType::Pointer: {
                values[j] = &SRCLANG_VALUE_AS_OBJECT(*i)->pointer;
                types[j] = &ffi_type_pointer;
            }
                break;
            case ValueType::Decimal: {
                values[j] = &(*i);
                types[j] = &ffi_type_double;
            }
                break;

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
    ffi_type *ret_type = &ffi_type_slong;
    if (native->ret == ValueType::Decimal) ret_type = &ffi_type_double;
    else if (native->ret == ValueType::Pointer) ret_type = &ffi_type_pointer;

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, count, ret_type,
                     types) != FFI_OK) {
        error("ffi_prep_cif() failed");
        return false;
    }
    ffi_arg result;
    Value result_value;
    ffi_call(&cif, FFI_FN(handler), &result, values);
    switch (native->ret) {
        case ValueType::Boolean:
            result_value = SRCLANG_VALUE_BOOL(result != 0);
            break;
        case ValueType::Char:
            result_value = SRCLANG_VALUE_CHAR(result);
            break;
        case ValueType::Integer:
            result_value = SRCLANG_VALUE_INTEGER(result);
            break;

        case ValueType::Pointer:
            if ((void *) result == nullptr) {
                result_value = SRCLANG_VALUE_NULL;
            } else {
                result_value = SRCLANG_VALUE_POINTER((void *) result);
            }

            break;

        case ValueType::Decimal:
            result_value = SRCLANG_VALUE_DECIMAL(result);
            break;
        case ValueType::String:
            result_value = SRCLANG_VALUE_STRING((char *) result);
            break;
        default:
            error("ERROR: unsupported return type '" +
                  SRCLANG_VALUE_TYPE_ID[int(native->ret)] + "'");
            return false;
    }

    sp -= count + 1;
    *sp++ = result_value;
    return true;
}

bool Interpreter::call_bounded(Value callee, uint8_t count) {
    auto bounded = (BoundedValue *) SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
    *(sp - count - 1) = bounded->value;
    for (auto i = sp; i != sp - count; i--) {
        *i = *(i - 1);
    }
    *(sp - count) = bounded->parent;
    sp++;
    if (debug) {
        std::cout << std::endl;
        print_stack();
    }

    return call(count + 1);
}


bool Interpreter::call(uint8_t count) {
    auto callee = *(sp - 1 - count);
    if (SRCLANG_VALUE_IS_TYPE(callee)) {
        return call_typecast(callee, count);
    } else if (SRCLANG_VALUE_IS_OBJECT(callee)) {
        switch (SRCLANG_VALUE_AS_OBJECT(callee)->type) {
            case ValueType::Closure:
                return call_closure(callee, count);
            case ValueType::Builtin:
                return call_builtin(callee, count);
            case ValueType::Native:
                return call_native(callee, count);
            case ValueType::Bounded:
                return call_bounded(callee, count);
            default:
                error("ERROR: can't call object '" +
                      SRCLANG_VALUE_DEBUG(callee) +
                      "'");
                return false;
        }
    }
    error("ERROR: can't call value of type '" +
          SRCLANG_VALUE_DEBUG(callee) +
          "'");
    return false;
}

bool Interpreter::run() {
    while (true) {
        if (debug) {
            if (!debug_info.empty() && debug_info.back() != nullptr) {
                std::cout << debug_info.back()->filename << ":"
                          << debug_info.back()->lines[distance(
                                  cur()->closure->fun->instructions->begin(), cur()->ip)]
                          << std::endl;
            }

            std::cout << "  ";
            for (auto i = stack.begin(); i != sp; i++) {
                std::cout << "[" << SRCLANG_VALUE_DEBUG(*i) << "] ";
            }
            std::cout << std::endl;
            std::cout << ">> ";
            ByteCode::debug(
                    *cur()->closure->fun->instructions.get(), language->constants,
                    distance(cur()->closure->fun->instructions->begin(), ip()), std::cout);
            std::cout << std::endl;

            if (break_) std::cin.get();
        }
        auto inst = static_cast<OpCode>(*ip()++);
        switch (inst) {
            case OpCode::CONST:
                *sp++ = language->constants[*ip()++];
                break;
            case OpCode::CONST_INT:
                *sp++ = SRCLANG_VALUE_INTEGER((*ip()++));
                break;
            case OpCode::CONST_NULL:
                *sp++ = SRCLANG_VALUE_NULL;
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
                auto b = *--sp;
                auto a = *--sp;
                if (!binary(a, b, inst)) {
                    return false;
                }
            }
                break;

            case OpCode::COMMAND:
            case OpCode::NOT:
            case OpCode::NEG: {
                if (!unary(*--sp, inst)) {
                    return false;
                }
            }
                break;

            case OpCode::STORE: {
                auto scope = Symbol::Scope(*ip()++);
                int pos = *ip()++;

                switch (scope) {
                    case Symbol::Scope::LOCAL:
                        *(cur()->bp + pos) = *(sp - 1);
                        break;
                    case Symbol::Scope::GLOBAL:
                        if (pos >= language->globals.size()) {
                            error("GLOBALS SYMBOLS OVERFLOW");
                            return false;
                        }
                        language->globals[pos] = *(sp - 1);
                        break;
                    default:
                        error("Invalid STORE operation on '" +
                              SRCLANG_SYMBOL_ID[int(scope)] + "'");
                        return false;
                }
            }
                break;

            case OpCode::LOAD: {
                auto scope = Symbol::Scope(*ip()++);
                int pos = *ip()++;
                switch (scope) {
                    case Symbol::Scope::LOCAL:
                        *sp++ = *(cur()->bp + pos);
                        break;
                    case Symbol::Scope::GLOBAL:
                        *sp++ = language->globals[pos];
                        break;
                    case Symbol::Scope::BUILTIN:
                        *sp++ = builtins[pos];
                        break;
                    case Symbol::Scope::TYPE:
                        *sp++ = SRCLANG_VALUE_TYPES[pos];
                        break;
                    case Symbol::Scope::FREE:
                        *sp++ = cur()->closure->free[pos];
                        break;
                    default:
                        error("ERROR: can't load value of scope '" +
                              SRCLANG_SYMBOL_ID[int(scope)] + "'");
                        return false;
                }

            }
                break;

            case OpCode::CLOSURE: {
                int funIndex = *ip()++;
                int nfree = *ip()++;

                auto constant = language->constants[funIndex];
                auto fun = (Function *) SRCLANG_VALUE_AS_OBJECT(constant)->pointer;
                auto frees = std::vector<Value>(sp - nfree, sp);
                sp -= nfree;
                auto closure = new Closure{fun, frees};
                auto closure_value = SRCLANG_VALUE_CLOSURE(closure);
                *sp++ = closure_value;
                add_object(closure_value);
            }
                break;

            case OpCode::CALL: {
                int count = *ip()++;
                if (!call(count)) {
                    return false;
                }
            }
                break;

            case OpCode::POP: {
                if (sp == stack.begin()) {
                    error("Stack-underflow");
                    return false;
                }
                *--sp;
            }
                break;

            case OpCode::PACK: {
                auto size = *ip()++;
                auto list = new std::vector<Value>(sp - size, sp);
                sp -= size;
                auto list_value = SRCLANG_VALUE_LIST(list);
                add_object(list_value);
                *sp++ = list_value;
            }
                break;

            case OpCode::MAP: {
                auto size = *ip()++;
                auto map = new SrcLangMap();
                for (auto i = sp - (size * 2); i != sp; i += 2) {
                    map->insert(
                            {(char *) SRCLANG_VALUE_AS_OBJECT(*(i))->pointer,
                             *(i + 1)});
                }
                sp -= (size * 2);
                *sp++ = SRCLANG_VALUE_MAP(map);
                add_object(*(sp - 1));
            }
                break;

            case OpCode::INDEX: {
                auto count = *ip()++;
                Value pos, end_idx;
                switch (count) {
                    case 1:
                        pos = *--sp;
                        break;
                    case 2:
                        end_idx = *--sp;
                        pos = *--sp;
                        if (!(SRCLANG_VALUE_IS_INTEGER(pos) &&
                              SRCLANG_VALUE_IS_INTEGER(end_idx))) {
                            error("invalid INDEX for range");
                            return false;
                        }
                        break;
                    default:
                        error("invalid INDEX count");
                        return false;
                }
                auto container = *--sp;
                if (SRCLANG_VALUE_GET_TYPE(container) ==
                    ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Integer) {
                    char *buffer =
                            (char *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                    int index = SRCLANG_VALUE_AS_INTEGER(pos);
                    int len = strlen(buffer);
                    switch (count) {
                        case 1: {
                            if (strlen(buffer) <= index || index < 0) {
                                error(
                                        "ERROR: out-of-index trying to access "
                                        "'" +
                                        std::to_string(index) +
                                        "' from string of size '" +
                                        std::to_string(strlen(buffer)) + "'");
                                return false;
                            }
                            *sp++ = SRCLANG_VALUE_CHAR(buffer[index]);
                        }
                            break;
                        case 2: {
                            int end = SRCLANG_VALUE_AS_INTEGER(end_idx);
                            if (index < 0) index = len + index;
                            if (end < 0) end = len + end + 1;
                            if (end - index < 0 || end - index >= len) {
                                error("Out-of-range");
                                return false;
                            }
                            char *buf =
                                    strndup(buffer + index, end - index);
                            buf[end - index] = '\0';
                            *sp++ = SRCLANG_VALUE_STRING(buf);

                            add_object(*(sp - 1));
                        }
                            break;
                        default:
                            error(
                                    "Invalid INDEX range operation for string");
                            return false;
                    }

                } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                           ValueType::List &&
                           SRCLANG_VALUE_GET_TYPE(pos) ==
                           ValueType::Integer) {
                    std::vector<Value> list =
                            *(std::vector<Value> *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;

                    int index = SRCLANG_VALUE_AS_INTEGER(pos);
                    switch (count) {
                        case 1:
                            if (list.size() <= index || index < 0) {
                                error("out-of-index trying to access '" +
                                      std::to_string(index) +
                                      "' from list of size '" +
                                      std::to_string(list.size()) + "'");
                                return false;
                            }
                            *sp++ = list[index];
                            break;
                        case 2: {
                            int end = SRCLANG_VALUE_AS_INTEGER(end_idx);
                            if (index < 0) index = list.size() + index;
                            if (end < 0) end = list.size() + end + 1;
                            if (end - index < 0) {
                                error("Invalid range value");
                                return false;
                            }
                            auto values = new SrcLangList(end - index);
                            for (int i = index; i < end; i++) {
                                values->at(i) =
                                        builtin_clone({list[i]}, this);
                            }
                            *sp++ = SRCLANG_VALUE_LIST(values);
                            add_object(*(sp - 1));

                        }
                            break;
                        default:
                            error("Invalid INDEX range operation for list");
                            return false;
                    }

                } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                           ValueType::Map &&
                           SRCLANG_VALUE_GET_TYPE(pos) ==
                           ValueType::String) {
                    auto map = *reinterpret_cast<SrcLangMap *>(
                            SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    auto buf = reinterpret_cast<char *>(
                            SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                    auto idx = map.find(buf);
                    if (idx == map.end()) {
                        error("KeyNotFound '" + SRCLANG_VALUE_DEBUG(pos) + "' in container");
                        return false;
                    } else {
                        *sp++ = idx->second;
                    }
                } else {
                    if (SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                        auto property =
                                (char *) (SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                        error("KeyNotFound '" + SRCLANG_VALUE_DEBUG(pos) + "' in container");
                        return false;
                    } else {
                        error("InvalidOperation b/w '" + SRCLANG_VALUE_DEBUG(pos) + "' and '" +
                              SRCLANG_VALUE_DEBUG(container) + "'");
                        return false;
                    }
                }
            }
                break;

            case OpCode::SET_SELF: {
                auto freeIndex = *ip()++;

                auto currentClosure = cur()->closure;
                currentClosure->free[freeIndex] = SRCLANG_VALUE_CLOSURE(currentClosure);
            }
                break;

            case OpCode::SET: {
                auto val = *--sp;
                auto pos = *--sp;
                auto container = *--sp;
                if (SRCLANG_VALUE_GET_TYPE(container) ==
                    ValueType::String &&
                    SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Integer) {
                    auto idx = SRCLANG_VALUE_AS_INTEGER(pos);
                    char *buf =
                            (char *) SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                    int size = strlen(buf);
                    if (idx < 0 || size <= idx) {
                        error("out of bound");
                        return false;
                    }
                    if (SRCLANG_VALUE_IS_CHAR(val)) {
                        buf = (char *) realloc(buf, size);
                        if (buf == nullptr) {
                            error("out of memory");
                            return false;
                        }
                        buf[idx] = SRCLANG_VALUE_AS_CHAR(val);
                    } else if (SRCLANG_VALUE_GET_TYPE(val) ==
                               ValueType::String) {
                        char *b =
                                (char *) SRCLANG_VALUE_AS_OBJECT(val)->pointer;
                        size_t b_size = strlen(b);
                        buf = (char *) realloc(buf, size + b_size);
                        strcat(buf, b);
                    } else {
                        error("can't SET '" +
                              SRCLANG_VALUE_TYPE_ID[int(
                                      SRCLANG_VALUE_GET_TYPE(val))] +
                              "' to string");
                        return true;
                    }
                } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                           ValueType::List &&
                           SRCLANG_VALUE_GET_TYPE(pos) ==
                           ValueType::Integer) {
                    auto list = reinterpret_cast<SrcLangList *>(
                            SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    list->at(SRCLANG_VALUE_AS_INTEGER(pos)) = val;
                } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                           ValueType::Map &&
                           SRCLANG_VALUE_GET_TYPE(pos) ==
                           ValueType::String) {
                    auto map = reinterpret_cast<SrcLangMap *>(
                            SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                    auto buf = reinterpret_cast<char *>(
                            SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                    if (map->find(buf) == map->end()) {
                        map->insert({buf, val});
                    } else {
                        map->at(buf) = val;
                    }
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
                *sp++ = container;
            }
                break;

            case OpCode::SIZE: {
                auto val = *--sp;
                if (!SRCLANG_VALUE_IS_OBJECT(val)) {
                    error("container in loop is not iterable");
                    return false;
                }
                auto obj = SRCLANG_VALUE_AS_OBJECT(val);
                switch (obj->type) {
                    case ValueType::String:
                        *sp++ = SRCLANG_VALUE_INTEGER(strlen((char *) obj->pointer));
                        break;
                    case ValueType::List:
                        *sp++ = SRCLANG_VALUE_INTEGER(((SrcLangList *) obj->pointer)->size());
                        break;
                    default:
                        error("container '" + SRCLANG_VALUE_GET_STRING(val) + "' is not a iterable object");
                        return false;
                }
            }
                break;

            case OpCode::RET: {
                auto value = *--sp;
                for (unsigned long &defer: std::ranges::reverse_view(cur()->defers)) {
                    language->call(defer, {});
                }
                sp = cur()->bp - 1;
                fp--;
                *sp++ = value;
                debug_info.pop_back();
            }
                break;

            case OpCode::JNZ: {
                auto value = *--sp;
                if (!SRCLANG_VALUE_AS_BOOL(value)) {
                    ip() = (cur()->closure->fun->instructions->begin() + *ip());
                } else {
                    *ip()++;
                }
            }
                break;

            case OpCode::DEFER: {
                auto fn = *--sp;
                cur()->defers.push_back(fn);
            }
                break;

            case OpCode::CONTINUE:
            case OpCode::BREAK:
            case OpCode::JMP: {
                ip() = (cur()->closure->fun->instructions->begin() + *ip());
            }
                break;

            case OpCode::HLT: {
                for (unsigned long &defer: std::ranges::reverse_view(cur()->defers)) {
                    language->call(defer, {});
                }
                return true;
            }
                break;

            default:
                error("unknown opcode '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(inst)] + "'");
                return false;
        }
    }
}
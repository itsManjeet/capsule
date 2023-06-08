#include <dlfcn.h>
#include <ffi.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace std;

#define SRCLANG_VERSION 20230221
const char *MAGIC_CODE = "SRCLANG";
const auto LOGO = R"(
                       .__                         
  _____________   ____ |  | _____    ____    ____  
 /  ___/\_  __ \_/ ___\|  | \__  \  /    \  / ___\
 \___ \  |  | \/\  \___|  |__/ __ \|   |  \/ /_/  >
/____  > |__|    \___  >____(____  /___|  /\___  /
     \/              \/          \/     \//_____/

)";

using Iterator = string::iterator;
using Byte = uint32_t;

#define SRCLANG_TOKEN_TYPE_LIST \
    X(Reserved)                 \
    X(Character)                \
    X(Identifier)               \
    X(String)                   \
    X(Number)                   \
    X(Eof)

enum class TokenType : uint8_t {
#define X(id) id,
    SRCLANG_TOKEN_TYPE_LIST
#undef X
};

const vector<string> SRCLANG_TOKEN_ID = {
#define X(id) #id,
        SRCLANG_TOKEN_TYPE_LIST
#undef X
};

template<typename Iterator>
struct Token {
    TokenType type;
    string literal;
    Iterator pos;

    friend ostream &operator<<(ostream &os, const Token token) {
        os << SRCLANG_TOKEN_ID[static_cast<int>(token.type)] << ":"
           << token.literal;
        return os;
    }
};

#define SRCLANG_OPCODE_LIST \
    X(CONST, 1)                \
    X(CONST_INT, 1)            \
    X(CONST_NULL, 1)           \
    X(LOAD, 2)                 \
    X(STORE, 2)                \
    X(ADD, 0)                  \
    X(SUB, 0)                  \
    X(MUL, 0)                  \
    X(DIV, 0)                  \
    X(NEG, 0)                  \
    X(NOT, 0)                  \
    X(COMMAND, 0)              \
    X(EQ, 0)                   \
    X(NE, 0)                   \
    X(LT, 0)                   \
    X(LE, 0)                   \
    X(GT, 0)                   \
    X(GE, 0)                   \
    X(AND, 0)                  \
    X(OR, 0)                   \
    X(LAND, 0)                 \
    X(LOR, 0)                  \
    X(LSHIFT, 0)               \
    X(RSHIFT, 0)               \
    X(MOD, 0)                  \
    X(BREAK, 1)                \
    X(CONTINUE, 1)             \
    X(CLOSURE, 2)              \
    X(CALL, 1)                 \
    X(PACK, 0)                 \
    X(MAP, 0)                  \
    X(INDEX, 2)                \
    X(SET, 0)                  \
    X(SET_SELF, 1)             \
    X(POP, 0)                  \
    X(RET, 0)                  \
    X(JNZ, 1)                  \
    X(JMP, 1)                  \
    X(SIZE, 0)                 \
    X(IMPL, 0)                 \
    X(HLT, 0)

enum class OpCode : uint8_t {
#define X(id, size) id,
    SRCLANG_OPCODE_LIST
#undef X
};

const vector<string> SRCLANG_OPCODE_ID = {
#define X(id, size) #id,
        SRCLANG_OPCODE_LIST
#undef X
};

const vector<int> SRCLANG_OPCODE_SIZE = {
#define X(id, size) size,
        SRCLANG_OPCODE_LIST
#undef X
};

#define SRCLANG_SYMBOL_SCOPE_LIST \
    X(BUILTIN)                    \
    X(GLOBAL)                     \
    X(LOCAL)                      \
    X(FREE)                       \
    X(TYPE)

struct Symbol {
    string name;
    enum Scope {
#define X(id) id,
        SRCLANG_SYMBOL_SCOPE_LIST
#undef X
    } scope;
    int index;
};

const vector<string> SRCLANG_SYMBOL_ID = {
#define X(id) #id,
        SRCLANG_SYMBOL_SCOPE_LIST
#undef X
};

#define SRCLANG_VALUE_TYPE_LIST \
    X(Null, "null_t")           \
    X(Boolean, "bool")          \
    X(Decimal, "float")         \
    X(Integer, "int")           \
    X(Char, "char")             \
    X(String, "str")            \
    X(List, "list")             \
    X(Map, "map")               \
    X(Function, "function")     \
    X(Closure, "closure")       \
    X(Builtin, "builtin")       \
    X(Native, "native")         \
    X(Error, "error")           \
    X(Bounded, "bounded")       \
    X(Type, "type")             \
    X(Pointer, "ptr")

enum class ValueType : uint8_t {
#define X(id, name) id,
    SRCLANG_VALUE_TYPE_LIST
#undef X
};

static const vector<string> SRCLANG_VALUE_TYPE_ID = {
#define X(id, name) name,
        SRCLANG_VALUE_TYPE_LIST
#undef X
};

typedef uint64_t Value;

#define SRCLANG_VALUE_SIGN_BIT ((uint64_t)0x8000000000000000)
#define SRCLANG_VALUE_QNAN ((uint64_t)0x7ffc000000000000)

#define SRCLANG_VALUE_TAG_NULL 1
#define SRCLANG_VALUE_TAG_FALSE 2
#define SRCLANG_VALUE_TAG_TRUE 3
#define SRCLANG_VALUE_TAG_INT 4
#define SRCLANG_VALUE_TAG_TYPE 5
#define SRCLANG_VALUE_TAG_CHAR 6
#define SRCLANG_VALUE_TAG_3 7

#define SRCLANG_VALUE_IS_BOOL(val) (((val) | 1) == SRCLANG_VALUE_TRUE)
#define SRCLANG_VALUE_IS_NULL(val) ((val) == SRCLANG_VALUE_NULL)
#define SRCLANG_VALUE_IS_DECIMAL(val) \
    (((val)&SRCLANG_VALUE_QNAN) != SRCLANG_VALUE_QNAN)
#define SRCLANG_VALUE_IS_OBJECT(val)                            \
    (((val) & (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_SIGN_BIT)) == \
     (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_SIGN_BIT))

#define SRCLANG_VALUE_IS_INTEGER(val)                          \
    (((val) & (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_INT)) == \
         (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_INT) &&       \
     ((val | SRCLANG_VALUE_SIGN_BIT) != val))

#define SRCLANG_VALUE_IS_TYPE(val)                              \
    (((val) & (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_TYPE)) == \
         (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_TYPE) &&       \
     ((val | SRCLANG_VALUE_SIGN_BIT) != val))

#define SRCLANG_VALUE_IS_CHAR(val)                              \
    (((val) & (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_CHAR)) == \
         (SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_CHAR) &&       \
     ((val | SRCLANG_VALUE_SIGN_BIT) != val))

#define SRCLANG_VALUE_AS_BOOL(val) ((val) == SRCLANG_VALUE_TRUE)
#define SRCLANG_VALUE_AS_DECIMAL(val) (srclang_value_to_decimal(val))
#define SRCLANG_VALUE_AS_OBJECT(val)  \
    ((HeapObject*)(uintptr_t)((val) & \
                              ~(SRCLANG_VALUE_SIGN_BIT | SRCLANG_VALUE_QNAN)))
#define SRCLANG_VALUE_AS_TYPE(val) ((ValueType)((val) >> 3))
#define SRCLANG_VALUE_AS_INTEGER(val) ((int)((val) >> 3))
#define SRCLANG_VALUE_AS_CHAR(val) ((char)((val) >> 3))

#define SRCLANG_VALUE_BOOL(b) ((b) ? SRCLANG_VALUE_TRUE : SRCLANG_VALUE_FALSE)
#define SRCLANG_VALUE_DECIMAL(num) (srclang_decimal_to_value(num))
#define SRCLANG_VALUE_OBJECT(obj)                         \
    (Value)(SRCLANG_VALUE_SIGN_BIT | SRCLANG_VALUE_QNAN | \
            (uint64_t)(uintptr_t)(obj))
#define SRCLANG_VALUE_TYPE(ty)                                      \
    ((Value)(SRCLANG_VALUE_QNAN | ((uint64_t)(uint32_t)(ty) << 3) | \
             SRCLANG_VALUE_TAG_TYPE))
#define SRCLANG_VALUE_INTEGER(val)                                   \
    ((Value)(SRCLANG_VALUE_QNAN | ((uint64_t)(uint32_t)(val) << 3) | \
             SRCLANG_VALUE_TAG_INT))

#define SRCLANG_VALUE_CHAR(val)                                      \
    ((Value)(SRCLANG_VALUE_QNAN | ((uint64_t)(uint32_t)(val) << 3) | \
             SRCLANG_VALUE_TAG_CHAR))

#define SRCLANG_VALUE_HEAP_OBJECT(type, ptr) \
    SRCLANG_VALUE_OBJECT(                                         \
        (new HeapObject{(type), (ptr)}))

#define SRCLANG_VALUE_STRING(str)                       \
    SRCLANG_VALUE_HEAP_OBJECT(                          \
        ValueType::String, (void*)str)

#define SRCLANG_VALUE_LIST(list)                                              \
    SRCLANG_VALUE_HEAP_OBJECT(ValueType::List, (void*)list)

#define SRCLANG_VALUE_MAP(map)                                                \
    SRCLANG_VALUE_HEAP_OBJECT(                                                \
        ValueType::Map, (void*)map)

#define SRCLANG_VALUE_ERROR(err)                  \
    SRCLANG_VALUE_HEAP_OBJECT(                    \
        ValueType::Error, (void*)err)

#define SRCLANG_VALUE_NATIVE(native)                                          \
    SRCLANG_VALUE_HEAP_OBJECT(ValueType::Native, (void*)native)

#define SRCLANG_VALUE_BUILTIN(id)                                    \
    SRCLANG_VALUE_HEAP_OBJECT(                                       \
        ValueType::Builtin, (void*)builtin_##id)

#define SRCLANG_VALUE_FUNCTION(fun)                                           \
    SRCLANG_VALUE_HEAP_OBJECT(                                                \
        ValueType::Function, (void*)fun)

#define SRCLANG_VALUE_CLOSURE(fun)                                       \
    SRCLANG_VALUE_HEAP_OBJECT(                                                \
        ValueType::Closure, (void*)fun)

#define SRCLANG_VALUE_POINTER(ptr) SRCLANG_VALUE_HEAP_OBJECT(ValueType::Pointer, ptr)

#define SRCLANG_VALUE_TRUE \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_TRUE))
#define SRCLANG_VALUE_FALSE \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_FALSE))
#define SRCLANG_VALUE_NULL \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_NULL))

#define SRCLANG_BUILTIN(id)                       \
    Value builtin_##id(vector<Value> const& args, \
                       Interpreter<Byte>* interpreter)

#define SRCLANG_VALUE_DEBUG(val) SRCLANG_VALUE_GET_STRING(val) + ":" + SRCLANG_VALUE_TYPE_ID[(int)SRCLANG_VALUE_GET_TYPE(val)]

#define SRCLANG_CHECK_ARGS_EXACT(count)                                    \
    if (args.size() != count)                                              \
        throw std::runtime_error("Expected '" + std::to_string(count) +    \
                                 "' but '" + std::to_string(args.size()) + \
                                 "' provided");

#define SRCLANG_CHECK_ARGS_RANGE(start, end)                               \
    if (args.size() < start || args.size() > end)                          \
        throw std::runtime_error("Expected '" + std::to_string(start) +    \
                                 "':'" + std::to_string(end) + "' but '" + \
                                 std::to_string(args.size()) + "' provided");

#define SRCLANG_CHECK_ARGS_TYPE(pos, ty)                              \
    if (SRCLANG_VALUE_GET_TYPE(args[pos]) != ty)                      \
        throw std::runtime_error("Expected '" + std::to_string(pos) + \
                                 "' to be '" +                        \
                                 SRCLANG_VALUE_TYPE_ID[int(ty)] + "'");

void SRCLANG_VALUE_DUMP(Value v, ostream &os);

typedef vector<Value> SrcLangList;
typedef map<string, Value> SrcLangMap;

static inline double srclang_value_to_decimal(Value value) {
    double num;
    memcpy(&num, &value, sizeof(Value));
    return num;
}

static inline Value srclang_decimal_to_value(double num) {
    Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

struct HeapObject {
    ValueType type;
    void *pointer{nullptr};

    bool marked{false};
};

struct NativeFunction {
    string id;
    vector<ValueType> param;
    ValueType ret;
};

struct BoundedValue {
    Value parent;
    Value value;
};

static const vector<Value> SRCLANG_VALUE_TYPES = {
#define X(id, name) SRCLANG_VALUE_TYPE(ValueType::id),
        SRCLANG_VALUE_TYPE_LIST
#undef X
};

ValueType SRCLANG_VALUE_GET_TYPE(Value val) {
    if (SRCLANG_VALUE_IS_NULL(val)) return ValueType::Null;
    if (SRCLANG_VALUE_IS_BOOL(val)) return ValueType::Boolean;
    if (SRCLANG_VALUE_IS_DECIMAL(val)) return ValueType::Decimal;
    if (SRCLANG_VALUE_IS_CHAR(val)) return ValueType::Char;
    if (SRCLANG_VALUE_IS_TYPE(val)) return ValueType::Type;
    if (SRCLANG_VALUE_IS_INTEGER(val)) return ValueType::Integer;

    if (SRCLANG_VALUE_IS_OBJECT(val))
        return (SRCLANG_VALUE_AS_OBJECT(val)->type);
    throw runtime_error("invalid value '" + to_string((uint64_t) val) + "'");
}

string SRCLANG_VALUE_GET_STRING(Value val) {
    auto type = SRCLANG_VALUE_GET_TYPE(val);
    switch (type) {
        case ValueType::Null:
            return "null";
        case ValueType::Boolean:
            return SRCLANG_VALUE_AS_BOOL(val) ? "true" : "false";
        case ValueType::Decimal:
            return to_string(SRCLANG_VALUE_AS_DECIMAL(val));
        case ValueType::Integer:
            return to_string(SRCLANG_VALUE_AS_INTEGER(val));
        case ValueType::Char:
            return string(1, (char) SRCLANG_VALUE_AS_CHAR(val));
        case ValueType::Type:
            return "<type(" +
                   SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_AS_TYPE(val))] +
                   ")>";
        default:
            if (SRCLANG_VALUE_IS_OBJECT(val)) {
                auto object = SRCLANG_VALUE_AS_OBJECT(val);
                switch (type) {
                    case ValueType::String:
                    case ValueType::Error:
                        return (char *) object->pointer;
                    case ValueType::List: {
                        stringstream ss;
                        ss << "[";
                        string sep;
                        for (auto const &i: *(reinterpret_cast<SrcLangList *>(object->pointer))) {
                            ss << sep << SRCLANG_VALUE_GET_STRING(i);
                            sep = ", ";
                        }
                        ss << "]";
                        return ss.str();
                    }
                        break;

                    case ValueType::Map: {
                        stringstream ss;
                        ss << "{";
                        string sep;
                        for (auto const &i: *(reinterpret_cast<SrcLangMap *>(object->pointer))) {
                            ss << sep << i.first << ":" << SRCLANG_VALUE_GET_STRING(i.second);
                            sep = ", ";
                        }
                        ss << "}";
                        return ss.str();
                    }
                        break;

                    case ValueType::Function: {
                        return "<function()>";
                    }
                        break;

                    case ValueType::Pointer: {
                        stringstream ss;
                        ss << "0x" << std::hex << reinterpret_cast<unsigned long>(object->pointer);
                        return ss.str();
                    }

                    default:
                        return "<object(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
                }
            }
    }

    return "<value(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
}


struct SymbolTable {
    SymbolTable *parent{nullptr};
    map<string, Symbol> store;
    vector<Symbol> free;
    int definations{0};

    Symbol define(const string &name) {
        store[name] =
                Symbol{name, (parent == nullptr ? Symbol::GLOBAL : Symbol::LOCAL),
                       definations++};
        return store[name];
    }

    Symbol define(const string &name, int index) {
        store[name] = Symbol{name, Symbol::BUILTIN, index};
        return store[name];
    }

    Symbol define(const Symbol &other) {
        free.push_back(other);
        auto sym = Symbol{other.name, Symbol::FREE, (int) free.size() - 1};
        store[other.name] = sym;
        return sym;
    }

    optional<Symbol> resolve(const string &name) {
        auto iter = store.find(name);
        if (iter != store.end()) {
            return iter->second;
        }
        if (parent != nullptr) {
            auto sym = parent->resolve(name);
            if (sym == nullopt) {
                return sym;
            }

            if (sym->scope == Symbol::Scope::GLOBAL ||
                sym->scope == Symbol::Scope::BUILTIN ||
                sym->scope == Symbol::Scope::TYPE) {
                return sym;
            }
            return define(*sym);
        }
        return nullopt;
    }
};

struct DebugInfo {
    string filename;
    vector<int> lines;
    int position{};
};

template<typename Byte>
struct Instructions : vector<Byte> {
    Instructions() = default;

    OpCode last_instruction{};

    size_t emit(DebugInfo *debug_info, int line) { return 0; }

    template<typename T, typename... Types>
    size_t emit(DebugInfo *debug_info, int line, T byte, Types... operand) {
        size_t pos = this->size();
        this->push_back(static_cast<Byte>(byte));
        debug_info->lines.push_back(line);
        emit(debug_info, line, operand...);
        last_instruction = OpCode(byte);
        return pos;
    }
};

template<typename Byte, typename Constant>
struct ByteCode {
    unique_ptr<Instructions<Byte>> instructions;
    vector<Constant> constants;
    using Iterator = typename vector<Constant>::iterator;

    static int debug(Instructions<Byte> const &instructions,
                     vector<Constant> const &constants, int offset,
                     ostream &os) {
        os << setfill('0') << setw(4) << offset << " ";
        auto op = static_cast<OpCode>(instructions[offset]);
        os << SRCLANG_OPCODE_ID[static_cast<int>(op)];
        offset += 1;
        switch (op) {
            case OpCode::CONST: {
                int pos = instructions[offset++];
                if (constants.size() > 0) {
                    os << " " << pos << " '"
                       << SRCLANG_VALUE_DEBUG(constants[pos]) << "'";
                }

            }
                break;
            case OpCode::INDEX:
            case OpCode::PACK:
            case OpCode::MAP:
            case OpCode::SET_SELF: {
                os << " " << (int) instructions[offset++];
            }
                break;
            case OpCode::CONTINUE:
            case OpCode::BREAK:
            case OpCode::JNZ:
            case OpCode::JMP: {
                int pos = instructions[offset++];
                os << " '" << pos << "'";
            }
                break;
            case OpCode::LOAD:
            case OpCode::STORE: {
                int scope = instructions[offset++];
                int pos = instructions[offset++];
                os << " " << pos << " '" << SRCLANG_SYMBOL_ID[scope] << "'";
            }
                break;
            case OpCode::CLOSURE: {
                int constantIndex = instructions[offset++];
                int nfree = instructions[offset++];
                os << constants[constantIndex] << " " << nfree;
            }
                break;

            case OpCode::CONST_INT:
            case OpCode::CALL: {
                int count = instructions[offset++];
                os << " '" << count << "'";
            }
                break;
            default:
                offset += SRCLANG_OPCODE_SIZE[int(op)];
                break;
        }

        return offset;
    }

    friend ostream &operator<<(ostream &os,
                               const ByteCode<Byte, Constant> &bytecode) {
        os << "== CODE ==" << endl;
        for (int offset = 0; offset < bytecode.instructions->size();) {
            offset = ByteCode<Byte, Constant>::debug(
                    *bytecode.instructions, bytecode.constants, offset, os);
            os << endl;
        }
        os << "\n== CONSTANTS ==" << endl;
        for (auto i = 0; i < bytecode.constants.size(); i++) {
            os << i << " " << SRCLANG_VALUE_DEBUG(bytecode.constants[i])
               << endl;
        }
        return os;
    }
};

enum class FunctionType {
    Function, Method, Initializer, Native
};

template<typename Byte>
struct Function {
    FunctionType type{FunctionType::Function};
    string id;
    unique_ptr<Instructions<Byte>> instructions{nullptr};
    int nlocals{0};
    int nparams{0};
    bool is_variadic{false};
    shared_ptr<DebugInfo> debug_info{nullptr};
};

template<typename Byte, typename Constant>
struct Closure {
    Function<Byte> *fun;
    vector<Value> free{0};
};

template<typename T>
void dump_int(T size, ostream &os) {
    os.write(reinterpret_cast<const char *>(&size), sizeof(T));
}


void dump_string(const string &id, ostream &os) {
    size_t size = id.size();
    dump_int<size_t>(size, os);
    os.write(id.c_str(), size * sizeof(char));
}

template<typename T>
T read_int(istream &is) {
    T size;
    is.read(reinterpret_cast<char *>(&size), sizeof(T));
    return size;
}


string read_string(istream &is) {
    auto size = read_int<size_t>(is);
    char buffer[size + 1];
    is.read(buffer, size);
    buffer[size] = '\0';
    return buffer;
}

void SRCLANG_DEBUGINFO_DUMP(DebugInfo *debug_info, ostream &os) {
    dump_string(debug_info->filename, os);
    dump_int<int>(debug_info->position, os);
    dump_int<size_t>(debug_info->lines.size(), os);
    for (auto i: debug_info->lines) {
        dump_int<int>(i, os);
    }
}

shared_ptr<DebugInfo> SRCLANG_DEBUGINFO_READ(istream &is) {
    auto debug_info = make_shared<DebugInfo>();
    debug_info->filename = read_string(is);
    debug_info->position = read_int<int>(is);
    size_t size = read_int<size_t>(is);
    for (auto i = 0; i < size; i++) {
        debug_info->lines.push_back(read_int<int>(is));
    }
    return debug_info;
}

void SRCLANG_VALUE_DUMP(Value v, ostream &os) {
    auto valueType = SRCLANG_VALUE_GET_TYPE(v);
    os.write(reinterpret_cast<const char *>(&valueType), sizeof(ValueType));
    if (!SRCLANG_VALUE_IS_OBJECT(v)) {
        os.write(reinterpret_cast<const char *>(&v), sizeof(Value));
        return;
    }

    auto object = SRCLANG_VALUE_AS_OBJECT(v);
    object->type = valueType;
    switch (object->type) {
        case ValueType::String:
        case ValueType::Error:
            dump_string((const char *) object->pointer, os);
            break;
        case ValueType::List: {
            auto list = (SrcLangList *) object->pointer;
            dump_int<size_t>(list->size(), os);
            for (auto &i: *list) {
                SRCLANG_VALUE_DUMP(i, os);
            }
            break;
        }

        case ValueType::Map: {
            auto map_ = (SrcLangMap *) object->pointer;
            dump_int<size_t>(map_->size(), os);
            for (auto &i: *map_) {
                dump_string(i.first, os);
                SRCLANG_VALUE_DUMP(i.second, os);
            }
            break;
        }

        case ValueType::Function: {
            auto function = (Function<Byte> *) object->pointer;
            os.write(reinterpret_cast<const char *>(&function->type), sizeof(function->type));
            dump_string(function->id, os);
            dump_int<size_t>(function->instructions->size(), os);
            for (auto i: *function->instructions) {
                os.write(reinterpret_cast<const char *>(&i), sizeof(i));
            }
            dump_int<int>(function->nlocals, os);
            dump_int<int>(function->nparams, os);
            dump_int<bool>(function->is_variadic, os);
            SRCLANG_DEBUGINFO_DUMP(function->debug_info.get(), os);
        }

        default:
            throw runtime_error("can't dump value '" + SRCLANG_VALUE_TYPE_ID[int(object->type)] + "'");
    }
}

Value SRCLANG_VALUE_READ(istream &is) {
    ValueType valueType;
    is.read(reinterpret_cast<char *>(&valueType), sizeof(ValueType));
    if (valueType <= ValueType::Char) {
        Value value;
        is.read(reinterpret_cast<char *>(&value), sizeof(Value));
        return value;
    }
    auto object = new HeapObject();
    object->type = valueType;
    switch (object->type) {
        case ValueType::String:
        case ValueType::Error:
            object->pointer = (void *) strdup(read_string(is).c_str());
            break;
        case ValueType::List: {
            auto list = new SrcLangList();
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                list->push_back(SRCLANG_VALUE_READ(is));
            }
            object->pointer = (void *) list;
            break;
        }

        case ValueType::Map: {
            auto map_ = new SrcLangMap();
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                string k = read_string(is);
                Value v = SRCLANG_VALUE_READ(is);
                map_->insert({k, v});
            }
            object->pointer = (void *) map_;
            break;
        }

        case ValueType::Function: {
            auto function = new Function<Byte>();
            function->instructions = make_unique<Instructions<Byte>>();
            is.read(reinterpret_cast<char *>(&function->type), sizeof(function->type));
            function->id = read_string(is);
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                Byte byte;
                is.read(reinterpret_cast<char *>(&byte), sizeof(Byte));
                function->instructions->push_back(byte);
            }
            function->nlocals = read_int<int>(is);
            function->nparams = read_int<int>(is);
            function->is_variadic = read_int<bool>(is);
            function->debug_info = SRCLANG_DEBUGINFO_READ(is);
            object->pointer = (void *) function;
        }

        default:
            throw runtime_error("can't dump value '" + SRCLANG_VALUE_TYPE_ID[int(object->type)] + "'");
    }

    return SRCLANG_VALUE_OBJECT(object);
}

void SRCLANG_BYTECODE_DUMP(ByteCode<Byte, Value> const &bytecode, ostream &os) {
    dump_int<size_t>(bytecode.instructions->size(), os);
    for (auto byte: *bytecode.instructions) {
        dump_int<Byte>(byte, os);
    }
    dump_int<size_t>(bytecode.constants.size(), os);
    for (auto i: bytecode.constants) {
        SRCLANG_VALUE_DUMP(i, os);
    }
}

ByteCode<Byte, Value> SRCLANG_BYTECODE_READ(istream &is) {
    auto size = read_int<size_t>(is);
    ByteCode<Byte, Value> bytecode;
    bytecode.instructions = make_unique<Instructions<Byte>>();
    for (auto i = 0; i < size; i++) {
        bytecode.instructions->push_back(read_int<Byte>(is));
    }
    size = read_int<size_t>(is);
    for (auto i = 0; i < size; i++) {
        bytecode.constants.push_back(SRCLANG_VALUE_READ(is));
    }
    return std::move(bytecode);
}

void SRCLANG_VALUE_FREE(Value value) {
    if (!SRCLANG_VALUE_IS_OBJECT(value)) {
        return;
    }
    auto type = SRCLANG_VALUE_GET_TYPE(value);
    auto object = SRCLANG_VALUE_AS_OBJECT(value);
    switch (type) {
        case ValueType::String:
        case ValueType::Error:
            free((char *) object->pointer);
            break;

        case ValueType::List:
            delete reinterpret_cast<SrcLangList *>(object->pointer);
            break;

        case ValueType::Map:
            delete reinterpret_cast<SrcLangMap *>(object->pointer);
            break;

        case ValueType::Function:
            delete reinterpret_cast<Function<Byte> *>(object->pointer);
            break;

        case ValueType::Pointer:
            break;

        default:
            runtime_error("can't clean value of type '" + SRCLANG_VALUE_TYPE_ID[int(type)] + "'");
    }
    delete object;
}

struct MemoryManager {
    using Heap = vector<Value>;
    Heap heap;

    MemoryManager() = default;

    ~MemoryManager() = default;

    void mark(Value val) {
        if (SRCLANG_VALUE_IS_OBJECT(val)) {
            auto obj = SRCLANG_VALUE_AS_OBJECT(val);
            if (obj->marked) return;
            obj->marked = true;
#ifdef SRCLANG_GC_DEBUG
                                                                                                                                    cout << "  marked "
                 << uintptr_t(SRCLANG_VALUE_AS_OBJECT(val)->pointer) << "'"
                 << SRCLANG_VALUE_GET_STRING(val) << "'" << endl;
#endif
            if (obj->type == ValueType::List) {
                mark(reinterpret_cast<vector<Value> *>(obj->pointer)->begin(),
                     reinterpret_cast<vector<Value> *>(obj->pointer)->end());
            } else if (obj->type == ValueType::Closure) {
                mark(reinterpret_cast<Closure<Byte, Value> *>(obj->pointer)->free.begin(),
                     reinterpret_cast<Closure<Byte, Value> *>(obj->pointer)->free.end());
            } else if (obj->type == ValueType::Map) {
                for (auto &i: *reinterpret_cast<SrcLangMap *>(obj->pointer)) {
                    mark(i.second);
                }
            }
        }
    }

    void mark(Heap::iterator start, Heap::iterator end) {
        for (auto i = start; i != end; i++) {
            mark(*i);
        }
    }

    void sweep() {
        for (auto i = heap.begin(); i != heap.end();) {
            if (SRCLANG_VALUE_IS_OBJECT(*i)) {
                auto obj = SRCLANG_VALUE_AS_OBJECT(*i);
                if (obj->marked) {
                    obj->marked = false;
                    i++;
                } else {
#ifdef SRCLANG_GC_DEBUG
                                                                                                                                            cout << "   deallocating "
                                                << uintptr_t(obj->pointer) << "'"
                                                << SRCLANG_VALUE_GET_STRING(*i)
                                                << "'" << endl;
#endif
                    SRCLANG_VALUE_FREE(*i);
                    i = heap.erase(i);
                }
            }
        }
    }
};

template<typename Byte>
struct Interpreter;

typedef Value (*Builtin)(vector<Value> &, Interpreter<Byte> *);

template<class V>
type_info const &variant_typeid(V const &v) {
    return visit([](auto &&x) -> decltype(auto) { return typeid(x); }, v);
}

using OptionType = variant<string, int, float, bool>;
using Options = map<string, OptionType>;

template<typename Iterator, typename Byte>
struct Compiler {
    SymbolTable *symbol_table{nullptr};
    MemoryManager *memory_manager;
    SymbolTable *global;
    Token<Iterator> cur, peek;
    Iterator iter, start, end;
    string filename;
    vector<Value> &constants;
    vector<string> loaded_imports;
    vector<unique_ptr<Instructions<Byte>>> instructions;
    DebugInfo *debug_info;
    Options &options;
    shared_ptr<DebugInfo> global_debug_info;
    int fileConstantPosition = 0;

    Compiler(Iterator start, Iterator end, const string &filename, SymbolTable *global,
             vector<Value> &constants, MemoryManager *memory_manager, Options &options)
            : iter{start},
              start{start},
              end{end},
              constants{constants},
              global{global},
              symbol_table{global},
              filename{filename},
              memory_manager{memory_manager},
              options{options} {
        global_debug_info = make_shared<DebugInfo>();
        global_debug_info->filename = filename;
        global_debug_info->position = 0;
        debug_info = global_debug_info.get();
        instructions.push_back(make_unique<Instructions<Byte>>
                                       ());
        eat();
        eat();
        constants.push_back(SRCLANG_VALUE_STRING(strdup(filename.c_str())));
        fileConstantPosition = constants.size() - 1;
    }

    ByteCode<Byte, Value> code() {
        return ByteCode<Byte, Value>{std::move(instructions.back()), constants};
    }

    Instructions<Byte> *inst() { return instructions.back().get(); }

    void push_scope() {
        symbol_table = new SymbolTable{symbol_table};
        instructions.push_back(make_unique<Instructions<Byte>>
                                       ());
    }

    unique_ptr<Instructions<Byte>> pop_scope() {
        auto old = symbol_table;
        symbol_table = symbol_table->parent;
        delete old;
        auto res = std::move(instructions.back());
        instructions.pop_back();
        return std::move(res);
    }

    template<typename Message>
    void error(const Message &mesg, Iterator pos) const {
        int line;
        Iterator line_start = get_error_pos(pos, line);
        cout << filename << ":" << line << endl;
        if (pos != end) {
            cout << "ERROR: " << mesg << endl;
            cout << " | " << get_error_line(line_start) << endl << "   ";
            for (; line_start != pos; ++line_start) cout << ' ';
            cout << '^' << endl;
        } else {
            cout << "Unexpected end of file. ";
            cout << mesg << " line " << line << endl;
        }
    }

    Iterator get_error_pos(Iterator err_pos, int &line) const {
        line = 1;
        Iterator i = start;
        Iterator line_start = start;
        while (i != err_pos) {
            bool eol = false;
            if (i != err_pos && *i == '\r')  // CR
            {
                eol = true;
                line_start = ++i;
            }
            if (i != err_pos && *i == '\n')  // LF
            {
                eol = true;
                line_start = ++i;
            }
            if (eol)
                ++line;
            else
                ++i;
        }
        return line_start;
    }

    [[nodiscard]] string get_error_line(Iterator err_pos) const {
        Iterator i = err_pos;
        // position i to the next EOL
        while (i != end && (*i != '\r' && *i != '\n')) ++i;
        return string(err_pos, i);
    }

    bool consume(const string &expected) {
        if (cur.type != TokenType::Reserved ||
            expected.length() != cur.literal.length() ||
            !equal(expected.begin(), expected.end(), cur.literal.begin(),
                   cur.literal.end())) {
            return false;
        }
        return eat();
    }

    bool consume(TokenType type) {
        if (cur.type != type) {
            return false;
        }
        return eat();
    }

    bool check(TokenType type) {
        if (cur.type != type) {
            stringstream ss;
            ss << "Expected '" << SRCLANG_TOKEN_ID[static_cast<int>(type)]
               << "' but got '" << cur.literal << "'";
            error(ss.str(), cur.pos);
            return false;
        }
        return true;
    }

    bool expect(const string &expected) {
        if (cur.literal != expected) {
            stringstream ss;
            ss << "Expected '" << expected << "' but got '" << cur.literal
               << "'";
            error(ss.str(), cur.pos);
            return false;
        }
        return eat();
    }

    bool expect(TokenType type) {
        if (cur.type != type) {
            stringstream ss;
            ss << "Expected '" << SRCLANG_TOKEN_ID[int(type)] << "' but got '"
               << cur.literal << "'";
            error(ss.str(), cur.pos);
            return false;
        }
        return eat();
    }

    bool eat() {
        cur = peek;

        /// space ::= [ \t\n\r]
        while (isspace(*iter)) {
            iter++;
        }
        peek.pos = iter;
        if (iter == end) {
            peek.type = TokenType::Eof;
            return true;
        }

        auto escape = [&](Iterator &iterator, bool &status) -> char {
            if (*iterator == '\\') {
                char ch = *++iterator;
                iterator++;
                switch (ch) {
                    case 'a':
                        return '\a';
                    case 'b':
                        return '\b';
                    case 'n':
                        return '\n';
                    case 't':
                        return '\t';
                    case 'r':
                        return '\r';
                    case '\\':
                        return '\\';
                    case '\'':
                        return '\'';
                    default:
                        error("invalid escape sequence", iterator - 1);
                        status = false;
                }
            }
            return *iter++;
        };

        /// comment ::= '//' (.*) '\n'
        if (*iter == '/' && *(iter + 1) == '/') {
            iter += 2;
            while (*iter != '\n') {
                if (iter == end) {
                    peek.type = TokenType::Eof;
                    return true;
                }
                iter++;
            }
            iter++;
            return eat();
        }

        /// string ::= '"' ... '"'
        if (*iter == '"') {
            iter++;
            peek.literal.clear();
            bool status = true;
            while (*iter != '"') {
                if (iter == end) {
                    error("unterminated string", peek.pos);
                    return false;
                }
                peek.literal += escape(iter, status);
                if (!status) return false;
            }
            iter++;
            peek.type = TokenType::String;

            return true;
        }

        /// char :: '\'' <char> '\''
        if (*iter == '\'') {
            iter++;
            bool status = true;
            peek.literal = escape(iter, status);
            if (!status) return false;
            if (*iter != '\'') {
                error("unterminated character", peek.pos);
                return false;
            }
            iter++;
            peek.type = TokenType::Character;
            return true;
        }

        /// reserved ::=
        for (string k: {"let", "fun", "native", "return", "if", "else", "for",
                        "break", "continue", "import", "global", "impl", "as",
                        "in",

                // specical operators
                        "#!", "not", "...",

                // multi char operators
                        "==", "!=", "<=", ">=", ">>", "<<"}) {
            auto dist = distance(k.begin(), k.end());
            if (equal(iter, iter + dist, k.begin(), k.end()) &&
                !isalnum(*(iter + dist))) {
                iter += dist;
                peek.literal = string(k.begin(), k.end());
                peek.type = TokenType::Reserved;
                return true;
            }
        }

        /// identifier ::= [a-zA-Z_]([a-zA-Z0-9_]*)
        if (isalpha(*iter) || *iter == '_') {
            do {
                iter++;
            } while (isalnum(*iter) || *iter == '_');
            peek.literal = string_view(peek.pos, iter);
            peek.type = TokenType::Identifier;
            return true;
        }

        /// punct ::=
        if (ispunct(*iter)) {
            peek.literal = string_view(peek.pos, ++iter);
            peek.type = TokenType::Reserved;
            return true;
        }

        /// digit ::= [0-9]
        if (isdigit(*iter)) {
            do {
                iter++;
            } while (isdigit(*iter) || *iter == '.' || *iter == '_');
            if (*iter == 'b' ||
                *iter ==
                'h') {  // include 'b' for binary and 'h' for hexadecimal
                iter++;
            }
            peek.literal = string_view(peek.pos, iter);
            peek.type = TokenType::Number;
            return true;
        }
        error("unexpected token", iter);
        return false;
    }

    enum Precedence {
        P_None = 0,
        P_Assignment,
        P_Or,
        P_And,
        P_Lor,
        P_Land,
        P_Equality,
        P_Comparison,
        P_Shift,
        P_Term,
        P_Factor,
        P_Unary,
        P_Call,
        P_Primary,
    };

    Precedence precedence(string tok) {
        static map<string, Precedence> prec = {
                {":=",  P_Assignment},
                {"=",   P_Assignment},
                {"or",  P_Or},
                {"and", P_And},
                {"&",   P_Land},
                {"|",   P_Lor},
                {"==",  P_Equality},
                {"!=",  P_Equality},
                {">",   P_Comparison},
                {"<",   P_Comparison},
                {">=",  P_Comparison},
                {"<=",  P_Comparison},
                {">>",  P_Shift},
                {"<<",  P_Shift},
                {"+",   P_Term},
                {"-",   P_Term},
                {"*",   P_Factor},
                {"/",   P_Factor},
                {"%",   P_Factor},
                {"not", P_Unary},
                {"-",   P_Unary},
                {"$",   P_Unary},
                {".",   P_Call},
                {"[",   P_Call},
                {"(",   P_Call},
        };
        auto i = prec.find(tok);
        if (i == prec.end()) {
            return P_None;
        }
        return i->second;
    }

    template<typename T, typename... Ts>
    int emit(T t, Ts... ts) {
        int line;
        get_error_pos(cur.pos, line);
        return inst()->emit(debug_info, line, t, ts...);
    }

    bool number() {
        bool is_float = false;
        int base = 10;
        string number_value;
        if (cur.literal.starts_with("0") && cur.literal.length() > 1) {
            base = 8;
            cur.literal = cur.literal.substr(1);
        }
        for (auto i: cur.literal) {
            if (i == '.') {
                if (is_float) {
                    error("multiple floating point detected", cur.pos);
                    return false;
                }
                number_value += '.';
                is_float = true;
            } else if (i == '_') {
                continue;
            } else if (i == 'b') {
                base = 2;
            } else if (i == 'h') {
                base = 16;
            } else {
                number_value += i;
            }
        }
        Value val;
        try {
            if (is_float) {
                val = SRCLANG_VALUE_DECIMAL(stod(number_value));
            } else {
                val = SRCLANG_VALUE_INTEGER(stoi(number_value, 0, base));
            }
        } catch (std::invalid_argument const &e) {
            error("Invalid numerical value " + string(e.what()), cur.pos);
            return false;
        }

        constants.push_back(val);
        emit(OpCode::CONST, constants.size() - 1);
        return expect(TokenType::Number);
    }

    bool identifier(bool can_assign) {
        auto iter = find(SRCLANG_VALUE_TYPE_ID.begin(),
                         SRCLANG_VALUE_TYPE_ID.end(), cur.literal);
        if (iter != SRCLANG_VALUE_TYPE_ID.end()) {
            emit(OpCode::LOAD, Symbol::Scope::TYPE,
                 distance(SRCLANG_VALUE_TYPE_ID.begin(), iter));
            return eat();
        }
        auto symbol = symbol_table->resolve(cur.literal);
        if (symbol == nullopt) {
            error("undefined variable '" + cur.literal + "'", cur.pos);
            return false;
        }
        if (!eat()) return false;

        if (can_assign && consume("=")) {
            if (!expression()) return false;
            emit(OpCode::STORE, symbol->scope, symbol->index);
        } else {
            emit(OpCode::LOAD, symbol->scope, symbol->index);
        }
        return true;
    }

    bool string_() {
        auto string_value = SRCLANG_VALUE_STRING(strdup(cur.literal.c_str()));
        memory_manager->heap.push_back(string_value);
        constants.push_back(string_value);
        emit(OpCode::CONST, constants.size() - 1);
        return expect(TokenType::String);
    }

    bool char_() {
        constants.push_back(SRCLANG_VALUE_CHAR(cur.literal[0]));
        emit(OpCode::CONST, constants.size() - 1);
        return expect(TokenType::Character);
    }

    bool unary(OpCode op) {
        if (!expression(P_Unary)) {
            return false;
        }
        emit(op);
        return true;
    }

    bool block() {
        if (!expect("{")) return false;
        while (!consume("}")) {
            if (!statement()) {
                return false;
            }
        }
        return true;
    }

    /// fun '(' args ')' block
    bool function(Symbol *symbol) {
        bool is_variadic = false;
        auto pos = cur.pos;
        push_scope();
        if (symbol != nullptr) {
            auto freeSymbol = symbol_table->define(*symbol);
            emit(OpCode::SET_SELF, freeSymbol.index);
        }

        // eat '('
        if (!expect("(")) return false;
        int nparam = 0;
        while (!consume(")")) {
            if (!check(TokenType::Identifier)) {
                return false;
            }
            nparam++;
            symbol_table->define(cur.literal);
            eat();

            if (consume("...")) {
                if (!expect(")")) return false;
                is_variadic = true;
                break;
            }
            if (consume(")")) break;

            if (!expect(",")) return false;
        }

        auto fun_debug_info = make_shared<DebugInfo>();
        fun_debug_info->filename = filename;
        get_error_pos(pos, fun_debug_info->position);
        auto old_debug_info = debug_info;
        debug_info = fun_debug_info.get();

        if (!block()) return false;
        int line;
        get_error_pos(cur.pos, line);

        debug_info = old_debug_info;

        int nlocals = symbol_table->definations;
        auto free_symbols = symbol_table->free;

        auto fun_instructions = pop_scope();
        if (fun_instructions->last_instruction == OpCode::POP) {
            fun_instructions->pop_back();
            fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
        } else if (fun_instructions->last_instruction != OpCode::RET) {
            fun_instructions->emit(fun_debug_info.get(), line,
                                   OpCode::CONST_NULL);
            fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
        }

        for (auto const &i: free_symbols) {
            emit(OpCode::LOAD, i.scope, i.index);
        }

        static int function_count = 0;
        std::string id;
        if (symbol == nullptr) {
            id = to_string(function_count++);
        } else {
            id = symbol->name;
        }

        auto fun = new Function<Byte>{
                FunctionType::Function, id, std::move(fun_instructions), nlocals, nparam,
                is_variadic,
                fun_debug_info};
        auto fun_value = SRCLANG_VALUE_FUNCTION(fun);
        memory_manager->heap.push_back(fun_value);
        constants.push_back(fun_value);

        emit(OpCode::CLOSURE, constants.size() - 1, free_symbols.size());
        return true;
    }

    /// impl ::= 'impl' ("as" <string>) <identifier> 'for' <type>
    bool impl() {
        if (consume("as")) {
            if (!string_()) return false;
        } else {
            if (!check(TokenType::Identifier)) return false;
            auto id = cur.literal;
            constants.push_back(SRCLANG_VALUE_STRING(strdup(id.c_str())));
            emit(OpCode::CONST, constants.size() - 1);
        }

        if (!identifier(false)) return false;

        if (!expect("for")) return false;
        ValueType ty;

        try {
            ty = type(cur.literal);
            emit(OpCode::LOAD, Symbol::Scope::TYPE, int(ty));
        } catch (exception const &exc) {
            error(exc.what(), cur.pos);
            return false;
        }
        if (!eat()) return false;
        emit(OpCode::IMPL);
        return true;
    }

    /// list ::= '[' (<expression> % ',') ']'
    bool list() {
        int size = 0;
        while (!consume("]")) {
            if (!expression()) {
                return false;
            }
            size++;
            if (consume("]")) break;
            if (!consume(",")) return false;
        }
        emit(OpCode::PACK, size);
        return true;
    }

    /// map ::= '{' ((<identifier> ':' <expression>) % ',') '}'
    bool map_() {
        int size = 0;
        while (!consume("}")) {
            if (!check(TokenType::Identifier)) return false;
            constants.push_back(SRCLANG_VALUE_STRING(strdup(cur.literal.c_str())));
            emit(OpCode::CONST, constants.size() - 1);
            if (!eat()) return false;

            if (!expect(":")) return false;
            if (!expression()) return false;
            size++;
            if (consume("}")) break;
            if (!expect(",")) return false;
        }
        emit(OpCode::MAP, size);
        return true;
    }

    bool prefix(bool can_assign) {
        if (cur.type == TokenType::Number) {
            return number();
        } else if (cur.type == TokenType::String) {
            return string_();
        } else if (cur.type == TokenType::Identifier) {
            return identifier(can_assign);
        } else if (cur.type == TokenType::Character) {
            return char_();
        } else if (consume("not")) {
            return unary(OpCode::NOT);
        } else if (consume("-")) {
            return unary(OpCode::NEG);
        } else if (consume("$")) {
            return unary(OpCode::COMMAND);
        } else if (consume("[")) {
            return list();
        } else if (consume("{")) {
            return map_();
        } else if (consume("(")) {
            if (!expression()) {
                return false;
            }
            return consume(")");
        }

        error(
                "Unknown expression type '" + SRCLANG_TOKEN_ID[int(cur.type)] + "'",
                cur.pos);
        return false;
    }

    bool assign() {
        error("not yet implemented", cur.pos);
        return false;
    }

    bool binary(OpCode op, int prec) {
        if (!expression(prec + 1)) {
            return false;
        }
        emit(op);
        return true;
    }

    /// call ::= '(' (expr % ',' ) ')'
    bool call() {
        auto pos = cur.pos;
        int count = 0;
        while (!consume(")")) {
            count++;
            if (!expression()) {
                return false;
            }
            if (consume(")")) break;
            if (!expect(",")) return false;
        }
        if (count >= UINT8_MAX) {
            error("can't have arguments more that '" + to_string(UINT8_MAX) + "'", pos);
            return false;
        }
        emit(OpCode::CALL, count);
        return true;
    }

    /// index ::= <expression> '[' <expession> (':' <expression>)? ']'
    bool index(bool can_assign) {
        int count = 1;
        if (cur.literal == ":") {
            emit(OpCode::CONST_INT, 0);
        } else {
            if (!expression()) return false;
        }

        if (consume(":")) {
            count += 1;
            if (cur.literal == "]") {
                emit(OpCode::CONST_INT, -1);
            } else {
                if (!expression()) return false;
            }

        }
        if (!expect("]")) return false;
        if (can_assign && consume("=") && count == 1) {
            if (consume("fun")) {
                if (!function(nullptr)) return false;
            } else {
                if (!expression()) return false;
            }

            emit(OpCode::SET);
        } else {
            emit(OpCode::INDEX, count);
        }
        return true;
    }

    /// subscript ::= <expression> '.' <expression>
    bool subscript(bool can_assign) {
        if (!check(TokenType::Identifier)) return false;

        auto string_value = SRCLANG_VALUE_STRING(strdup(cur.literal.c_str()));
        memory_manager->heap.push_back(string_value);
        constants.push_back(string_value);
        emit(OpCode::CONST, constants.size() - 1);
        if (!eat()) return false;

        if (can_assign && consume("=")) {
            if (consume("fun")) {
                if (!function(nullptr)) return false;
            } else {
                if (!expression()) return false;
            }
            emit(OpCode::SET);
        } else {
            emit(OpCode::INDEX, 1);
        }

        return true;
    }

    bool infix(bool can_assign) {
        static map<string, OpCode> binop = {
                {"+",   OpCode::ADD},
                {"-",   OpCode::SUB},
                {"/",   OpCode::DIV},
                {"*",   OpCode::MUL},
                {"==",  OpCode::EQ},
                {"!=",  OpCode::NE},
                {"<",   OpCode::LT},
                {">",   OpCode::GT},
                {">=",  OpCode::GE},
                {"<=",  OpCode::LE},
                {"and", OpCode::AND},
                {"or",  OpCode::OR},
                {"|",   OpCode::LOR},
                {"&",   OpCode::LAND},
                {">>",  OpCode::LSHIFT},
                {"<<",  OpCode::RSHIFT},
                {"%",   OpCode::MOD},
        };

        if (consume("=")) {
            return assign();
        } else if (consume("(")) {
            return call();
        } else if (consume(".")) {
            return subscript(can_assign);
        } else if (consume("[")) {
            return index(can_assign);
        } else if (binop.find(cur.literal) != binop.end()) {
            string op = cur.literal;
            if (!eat()) return false;
            return binary(binop[op], precedence(op));
        }

        error("unexpected infix operation", cur.pos);
        return false;
    }

    bool expression(int prec = P_Assignment) {
        bool can_assign = prec <= P_Assignment;
        if (!prefix(can_assign)) {
            return false;
        }

        while ((cur.literal != ";" && cur.literal != "{") &&
               prec <= precedence(cur.literal)) {
            if (!infix(can_assign)) {
                return false;
            }
        }
        return true;
    }

    /// compiler_options ::= #![<option>(<value>)]
    bool compiler_options() {
        if (!expect("[")) return false;

        if (!check(TokenType::Identifier)) return false;
        auto option_id = cur.literal;
        auto pos = cur.pos;
        eat();

        auto id = options.find(option_id);
        if (id == options.end()) {
            error("unknown compiler option '" + option_id + "'", pos);
            return false;
        }
#define CHECK_TYPE_ID(ty)                                          \
    if (variant_typeid(id->second) != typeid(ty)) {                \
        error("invalid value of type '" +                          \
                  string(variant_typeid(id->second).name()) +      \
                  "' for option '" + option_id + "', required '" + \
                  string(typeid(ty).name()) + "'",                 \
              pos);                                                \
        return false;                                              \
    }
        OptionType value;
        if (consume("(")) {
            switch (cur.type) {
                case TokenType::Identifier:
                    if (cur.literal == "true" || cur.literal == "false") {
                        CHECK_TYPE_ID(bool);
                        value = cur.literal == "true";
                    } else {
                        CHECK_TYPE_ID(string);
                        value = cur.literal;
                    }
                    break;
                case TokenType::String:
                    CHECK_TYPE_ID(string);
                    value = cur.literal;
                    break;
                case TokenType::Number: {
                    bool is_float = false;
                    for (int i = 0; i < cur.literal.size(); i++)
                        if (cur.literal[i] == '.') is_float = true;

                    if (is_float) {
                        CHECK_TYPE_ID(float);
                        value = stof(cur.literal);
                    } else {
                        CHECK_TYPE_ID(int);
                        value = stoi(cur.literal);
                    }
                }
                    break;
                default:
                    CHECK_TYPE_ID(void);
            }
            eat();
            if (!expect(")")) return false;
        } else {
            CHECK_TYPE_ID(bool);
            value = true;
        }
#undef CHECK_TYPE_ID

        if (option_id == "VERSION") {
            if (SRCLANG_VERSION > get<float>(value)) {
                error(
                        "Code need srclang of version above or equal to "
                        "'" +
                        to_string(SRCLANG_VERSION) + "'",
                        pos);
                return false;
            }
        } else if (option_id == "SEARCH_PATH") {
            options[option_id] =
                    filesystem::absolute(get<string>(value)).string() + ":" + get<string>(options[option_id]);
        } else if (option_id == "C_LIBRARY") {
            void *handler = dlopen(get<string>(value).c_str(), RTLD_GLOBAL | RTLD_NOW);
            if (handler == nullptr) {
                error(dlerror(), cur.pos);
                return false;
            }
        } else {
            options[option_id] = value;
        }
        return expect("]");
    }

    /// let ::= 'let' 'global'? <identifier> '=' <expression>
    bool let() {
        bool is_global = symbol_table->parent == nullptr;
        if (consume("global")) is_global = true;

        if (!check(TokenType::Identifier)) {
            return false;
        }

        string id = cur.literal;
        auto s = is_global ? global : symbol_table;
        auto symbol = s->resolve(id);
        if (symbol.has_value()) {
            error("Variable already defined '" + id + "'", cur.pos);
            return false;
        }
        symbol = s->define(id);

        eat();

        if (!expect("=")) {
            return false;
        }
        if (consume("fun")) {
            if (!function(&(*symbol))) {
                return false;
            }
        } else if (consume("native")) {
            if (!native(&(*symbol))) {
                return false;
            }
        } else {
            if (!expression()) {
                return false;
            }
        }


        emit(OpCode::STORE, symbol->scope, symbol->index);
        emit(OpCode::POP);
        return expect(";");
    }

    bool return_() {
        if (!expression()) {
            return false;
        }
        emit(OpCode::RET);
        return expect(";");
    }

    void patch_loop(int loop_start, OpCode to_patch, int pos) {
        for (int i = loop_start; i < inst()->size();) {
            auto j = OpCode(inst()->at(i++));
            switch (j) {
                case OpCode::CONTINUE:
                case OpCode::BREAK: {
                    if (j == to_patch && inst()->at(i) == 0)
                        inst()->at(i++) = pos;
                }
                    break;

                default:
                    i += SRCLANG_OPCODE_SIZE[int(j)];
                    break;
            }
        }
    }

    /// loop ::= 'for' <expression> <block>
    /// loop ::= 'for' <identifier> 'in' <expression> <block>
    bool loop() {
        optional<Symbol> count, iter, temp_expr;
        static int loop_iterator = 0;
        static int temp_expr_count = 0;
        if (cur.type == TokenType::Identifier &&
            peek.type == TokenType::Reserved &&
            peek.literal == "in") {
            count = symbol_table->define("__iter_" + to_string(loop_iterator++) + "__");
            temp_expr = symbol_table->define("__temp_expr_" + to_string(temp_expr_count++) + "__");
            iter = symbol_table->resolve(cur.literal);
            if (iter == nullopt) iter = symbol_table->define(cur.literal);

            constants.push_back(SRCLANG_VALUE_INTEGER(0));
            emit(OpCode::CONST, constants.size() - 1);
            emit(OpCode::STORE, count->scope, count->index);
            emit(OpCode::POP);
            if (!eat()) return false;
            if (!expect("in")) return false;
        }

        auto loop_start = inst()->size();
        if (!expression()) return false;
        if (iter.has_value()) {
            emit(OpCode::STORE, temp_expr->scope, temp_expr->index);
        }

        int loop_exit;

        if (iter.has_value()) {
            emit(OpCode::SIZE);

            // perform index;
            emit(OpCode::LOAD, count->scope, count->index);
            emit(OpCode::GT);
            loop_exit = emit(OpCode::JNZ, 0);

            emit(OpCode::LOAD, temp_expr->scope, temp_expr->index);
            emit(OpCode::LOAD, count->scope, count->index);
            emit(OpCode::INDEX, 1);
            emit(OpCode::STORE, iter->scope, iter->index);
            emit(OpCode::POP);

            constants.push_back(SRCLANG_VALUE_INTEGER(1));
            emit(OpCode::CONST, constants.size() - 1);
            emit(OpCode::LOAD, count->scope, count->index);
            emit(OpCode::ADD);
            emit(OpCode::STORE, count->scope, count->index);

            emit(OpCode::POP);
        } else {
            loop_exit = emit(OpCode::JNZ, 0);
        }


        if (!block()) return false;

        patch_loop(loop_start, OpCode::CONTINUE, loop_start);

        emit(OpCode::JMP, loop_start);

        int after_condition = emit(OpCode::CONST_NULL);
        emit(OpCode::POP);

        inst()->at(loop_exit + 1) = after_condition;

        patch_loop(loop_start, OpCode::BREAK, after_condition);
        return true;
    }

    /// import ::= 'import' <string> ('as' <identifier>)?
    bool import_() {
        if (!check(TokenType::String)) return false;
        auto path = cur.literal;
        int line;
        get_error_pos(cur.pos, line);

        stringstream ss(get<string>(options["SEARCH_PATH"]));
        string search_path;
        bool found = false;
        while (getline(ss, search_path, ':')) {
            search_path += "/" + path + ".src";
            if (filesystem::exists(search_path)) {
                found = true;
                break;
            }
        }
        if (!found) {
            error("missing required module '" + path + "'", cur.pos);
            return false;
        }

        if (find(loaded_imports.begin(), loaded_imports.end(), search_path) !=
            loaded_imports.end()) {
            return true;
        }
        loaded_imports.push_back(search_path);

        ifstream reader(search_path);
        string input((istreambuf_iterator<char>(reader)),
                     (istreambuf_iterator<char>()));
        reader.close();

        auto old_symbol_table = symbol_table;
        symbol_table = new SymbolTable{old_symbol_table};

        Compiler<Iterator, Byte> compiler(input.begin(), input.end(),
                                          search_path, symbol_table, constants,
                                          memory_manager, options);
        if (!compiler.compile()) {
            error("failed to import '" + search_path + "'", cur.pos);
            return false;
        }


        auto instructions = std::move(compiler.code().instructions);
        instructions->pop_back();  // pop OpCode::HLT

        int total = 0;
        // export symbols
        for (auto i: symbol_table->store) {
            if (i.second.scope == Symbol::Scope::LOCAL &&
                isupper(i.first[0])) {
                constants.push_back(SRCLANG_VALUE_STRING(strdup(i.first.c_str())));
                instructions->emit(compiler.global_debug_info.get(), 0, OpCode::CONST, constants.size() - 1);
                instructions->emit(compiler.global_debug_info.get(), 0, OpCode::LOAD, i.second.scope,
                                   i.second.index);
                total++;
            }
        }
        int nlocals = symbol_table->definations;
        auto nfree = symbol_table->free;
        delete symbol_table;
        symbol_table = old_symbol_table;
        instructions->emit(compiler.global_debug_info.get(), 0, OpCode::MAP, total);
        instructions->emit(compiler.global_debug_info.get(), 0,
                           OpCode::RET);


        for (auto const &i: nfree) {
            emit(OpCode::LOAD, i.scope, i.index);
        }

        auto fun = new Function<Byte>{
                FunctionType::Function, "", std::move(instructions), nlocals, 0, false,
                compiler.global_debug_info};
        auto val = SRCLANG_VALUE_FUNCTION(fun);
        memory_manager->heap.push_back(val);
        constants.push_back(val);

        emit(OpCode::CLOSURE, constants.size() - 1, nfree.size());
        emit(OpCode::CALL, 0);
        if (!eat()) return false;

        string module_name = filesystem::path(path).filename().string();
        if (consume("as")) {
            if (!check(TokenType::Identifier)) return false;
            module_name = cur.literal;
            if (!eat()) return false;
        }
        if (symbol_table->resolve(module_name) != nullopt) {
            error("Can't import module with '" + module_name + "', variable already defined", cur.pos);
            return false;
        }
        fun->id = module_name;
        auto symbol = symbol_table->define(module_name);
        emit(OpCode::STORE, symbol.scope, symbol.index);
        emit(OpCode::POP);

        auto fileSymbol = symbol_table->resolve("__FILE__");
        emit(OpCode::CONST, fileConstantPosition);
        emit(OpCode::STORE, fileSymbol->scope, fileSymbol->index);
        emit(OpCode::POP);

        return true;
    }

    /// condition ::= 'if' <expression> <block> (else statement)?
    bool condition() {
        if (!expression()) {
            return false;
        }
        auto false_pos = emit(OpCode::JNZ, 0);
        if (!block()) {
            return false;
        }

        auto jmp_pos = emit(OpCode::JMP, 0);
        auto after_block_pos = inst()->size();

        inst()->at(false_pos + 1) = after_block_pos;

        if (consume("else")) {
            if (consume("if")) {
                if (!condition()) return false;
            } else {
                if (!block()) return false;
            }
        }

        auto after_alt_pos = emit(OpCode::CONST_NULL);
        emit(OpCode::POP);
        inst()->at(jmp_pos + 1) = after_alt_pos;

        return true;
    }

    ValueType type(string literal) {
        auto type = literal;
        auto iter = find(SRCLANG_VALUE_TYPE_ID.begin(),
                         SRCLANG_VALUE_TYPE_ID.end(), type);
        if (iter == SRCLANG_VALUE_TYPE_ID.end()) {
            throw runtime_error("Invalid type '" + type + "'");
        }
        return SRCLANG_VALUE_AS_TYPE(
                SRCLANG_VALUE_TYPES[distance(SRCLANG_VALUE_TYPE_ID.begin(), iter)]);
    };

    /// native ::= 'native' <identifier> ( (<type> % ',') ) <type>
    bool native(Symbol *symbol) {
        auto id = symbol->name;

        if (cur.type == TokenType::Identifier) {
            id = cur.literal;
            if (!eat()) return false;
        }
        vector<ValueType> types;
        ValueType ret_type;

        if (!expect("(")) return false;

        while (!consume(")")) {
            if (!check(TokenType::Identifier)) return false;
            try {
                types.push_back(type(cur.literal));
            } catch (runtime_error const &e) {
                error(e.what(), cur.pos);
                return false;
            }
            if (!eat()) return false;

            if (consume(")")) break;
            if (!expect(",")) return false;
        }
        if (!check(TokenType::Identifier)) return false;
        try {
            ret_type = type(cur.literal);
            if (!eat()) return false;
        } catch (runtime_error const &e) {
            error(e.what(), cur.pos);
            return false;
        }

        auto native = new NativeFunction{id, types, ret_type};
        Value val = SRCLANG_VALUE_NATIVE(native);
        memory_manager->heap.push_back(val);
        constants.push_back(val);
        emit(OpCode::CONST, constants.size() - 1);
        emit(OpCode::STORE, symbol->scope, symbol->index);
        return true;
    }

    /// statement ::= set
    ///           ::= let
    ///           ::= return
    ///           ::= ';'
    ///           ::= expression ';'
    bool statement() {
        if (consume("let"))
            return let();
        else if (consume("return"))
            return return_();
        else if (consume(";"))
            return true;
        else if (consume("if"))
            return condition();
        else if (consume("for"))
            return loop();
        else if (consume("import"))
            return import_();
        else if (consume("break")) {
            emit(OpCode::BREAK, 0);
            return true;
        } else if (consume("continue")) {
            emit(OpCode::CONTINUE, 0);
            return true;
        } else if (consume("impl")) {
            return impl();
        } else if (consume("#!")) {
            return compiler_options();
        }

        if (!expression()) {
            return false;
        }
        emit(OpCode::POP);
        return expect(";");
    }

    bool program() {
        while (cur.type != TokenType::Eof) {
            if (!statement()) {
                return false;
            }
        }
        return true;
    }

    bool compile() {
        auto fileSymbol = symbol_table->resolve("__FILE__");
        emit(OpCode::CONST, fileConstantPosition);
        emit(OpCode::STORE, fileSymbol->scope, fileSymbol->index);
        emit(OpCode::POP);
        if (!program()) {
            return false;
        }
        emit(OpCode::HLT);
        return true;
    }
};

#define SRCLANG_BUILTIN_LIST \
    X(println)               \
    X(gc)                    \
    X(len)                   \
    X(bind)                  \
    X(append)                \
    X(range)                 \
    X(clone)                 \
    X(pop)                   \
    X(lower)                 \
    X(upper)                 \
    X(search)

template<typename Byte>
struct Interpreter;
#define X(id) SRCLANG_BUILTIN(id);

SRCLANG_BUILTIN_LIST

#undef X

enum Builtins {
#define X(id) BUILTIN_##id,
    SRCLANG_BUILTIN_LIST
#undef X
};

vector<Value> builtins = {
#define X(id) SRCLANG_VALUE_BUILTIN(id),
        SRCLANG_BUILTIN_LIST
#undef X
};

template<typename Byte>
struct Interpreter {
    struct Frame {
        typename vector<Byte>::iterator ip;
        Closure<Byte, Value> *closure;
        vector<Value>::iterator bp;
    };

    map<ValueType, map<string, Value>> properites = {
#define ADD_BUILTIN_PROPERTY(id)         \
    {                                    \
#id, builtins[int(BUILTIN_##id)] \
    }
            {
                    ValueType::String,
                    {
                            ADD_BUILTIN_PROPERTY(len),
                            ADD_BUILTIN_PROPERTY(append),
                            ADD_BUILTIN_PROPERTY(pop),
                            ADD_BUILTIN_PROPERTY(clone),
                            ADD_BUILTIN_PROPERTY(upper),
                            ADD_BUILTIN_PROPERTY(lower),
                            ADD_BUILTIN_PROPERTY(search),
                    },
            },

            {
                    ValueType::List,
                    {
                            ADD_BUILTIN_PROPERTY(len),
                            ADD_BUILTIN_PROPERTY(append),
                            ADD_BUILTIN_PROPERTY(pop),
                            ADD_BUILTIN_PROPERTY(clone),
                            ADD_BUILTIN_PROPERTY(search),
                    },
            },
            {
                    ValueType::Map,
                    {
                            ADD_BUILTIN_PROPERTY(search),
                    },
            },
            {
                    ValueType::Type,
                    {
                            ADD_BUILTIN_PROPERTY(bind),
                    },
            }
#undef ADD_BUILTIN_PROPERTY
    };

    int next_gc = 50;
    float GC_HEAP_GROW_FACTOR = 1.0;
    int LIMIT_NEXT_GC = 200;

    vector<Value> stack;
    vector<Value>::iterator sp;
    MemoryManager *memory_manager;

    vector<Value> &globals;
    vector<Value> &constants;
    vector<Frame> frames;
    const Options &options;
    typename vector<Frame>::iterator fp;
    vector<shared_ptr<DebugInfo>> debug_info;
    bool debug, break_;

    void error(string const &mesg) {
        if (debug_info.empty() || debug_info.back() == nullptr) {
            cerr << "ERROR: " << mesg << endl;
            return;
        }

        cerr << debug_info.back()->filename << ":"
             << debug_info.back()->lines[distance(
                     cur()->closure->fun->instructions->begin(), cur()->ip)]
             << endl;
        cerr << "  ERROR: " << mesg << endl;
    }

    Interpreter(ByteCode<Byte, Value> &code, vector<Value> &globals,
                shared_ptr<DebugInfo> const &debug_info,
                MemoryManager *memory_manager, Options const &options)
            : stack(2048),
              frames(1024),
              globals{globals},
              constants{code.constants},
              memory_manager{memory_manager},
              options{options} {

        GC_HEAP_GROW_FACTOR =
                std::get<float>(options.at("GC_HEAP_GROW_FACTOR"));
        next_gc =
                std::get<int>(options.at("GC_INITIAL_TRIGGER"));
        sp = stack.begin();
        fp = frames.begin();
        this->debug_info.push_back(debug_info);
        auto fun = new Function<Byte>{FunctionType::Function, "<script>",
                                      std::move(code.instructions),
                                      0,
                                      0,
                                      false,
                                      debug_info};

        debug = get<bool>(options.at("DEBUG"));
        break_ = get<bool>(options.at("BREAK"));

        fp->closure = new Closure<Byte, Value>{std::move(fun), {}};
        fp->ip = fp->closure->fun->instructions->begin();
        fp->bp = sp;
        fp++;
    }

    ~Interpreter() {
        delete (frames.begin())->closure->fun;
        delete (frames.begin())->closure;
    }

    void add_object(Value val) {
#ifdef SRCLANG_GC_DEBUG
        gc();
#else
        if (memory_manager->heap.size() > next_gc && next_gc < LIMIT_NEXT_GC) {
            std::cout << "TRIGGERING GC:" << std::endl;
            gc();
            memory_manager->heap.shrink_to_fit();
            next_gc = memory_manager->heap.size() * GC_HEAP_GROW_FACTOR;
            std::cout << "NEXT GC:" << next_gc << std::endl;
        }
#endif
        memory_manager->heap.push_back(val);
    }

    constexpr typename vector<Byte>::iterator &ip() { return (fp - 1)->ip; }

    typename vector<Frame>::iterator cur() { return (fp - 1); }

    void gc() {
#ifdef SRCLANG_GC_DEBUG
                                                                                                                                cout << "Total allocations: " << memory_manager->heap.size() << endl;
        cout << "gc begin:" << endl;
#endif
        memory_manager->mark(stack.begin(), sp);
        memory_manager->mark(globals.begin(), globals.end());
        memory_manager->mark(constants.begin(), constants.end());
        memory_manager->mark(builtins.begin(), builtins.end());
        memory_manager->sweep();
#ifdef SRCLANG_GC_DEBUG
                                                                                                                                cout << "gc end:" << endl;
        cout << "Total allocations: " << memory_manager->heap.size() << endl;
#endif
    }

    bool isEqual(Value a, Value b) {
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

    bool unary(Value a, OpCode op) {
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
                    array<char, 128> buffer;
                    string result;
                    unique_ptr<FILE, decltype(&pclose)> pipe(
                            popen((char *) (SRCLANG_VALUE_AS_OBJECT(a))->pointer,
                                  "r"),
                            pclose);
                    if (!pipe) {
                        throw runtime_error("popen() failed!");
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

    bool binary(Value lhs, Value rhs, OpCode op) {
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

    bool is_falsy(Value val) {
        return SRCLANG_VALUE_IS_NULL(val) ||
               (SRCLANG_VALUE_IS_BOOL(val) &&
                SRCLANG_VALUE_AS_BOOL(val) == false) ||
               (SRCLANG_VALUE_IS_INTEGER(val) &&
                SRCLANG_VALUE_AS_INTEGER(val) == 0);
    }

    void print_stack() {
        if (debug) {
            cout << "  ";
            for (auto i = stack.begin(); i != sp; i++) {
                cout << "[" << SRCLANG_VALUE_GET_STRING(*i) << "] ";
            }
            cout << endl;
        }
    }

    bool call_closure(Value callee, uint8_t count) {
        auto closure = reinterpret_cast<Closure<Byte, Value> *>(
                SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        if (closure->fun->is_variadic) {
            if (count < closure->fun->nparams - 1) {
                error("expected atleast '" + to_string(closure->fun->nparams - 1) + "' but '" + to_string(count) +
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
            error("expected '" + to_string(closure->fun->nparams) + "' but '" + to_string(count) + "' provided");
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

    bool call_builtin(Value callee, uint8_t count) {
        auto builtin =
                reinterpret_cast<Builtin>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        vector<Value> args(sp - count, sp);
        sp -= count + 1;
        auto result = builtin(args, this);
        if (SRCLANG_VALUE_IS_OBJECT(result)) add_object(result);
        *sp++ = result;
        if (SRCLANG_VALUE_IS_OBJECT(result) &&
            SRCLANG_VALUE_AS_OBJECT(result)->type == ValueType::Error) {
            error((char *) SRCLANG_VALUE_AS_OBJECT(result)->pointer);
            return false;
        }

        return true;
    }

    bool call_typecast_int(uint8_t count) {
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
                        stoi((char *) (SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
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

    bool call_typecast_float(uint8_t count) {
        if (count != 1 || !SRCLANG_VALUE_IS_OBJECT(*(sp - count)) ||
            SRCLANG_VALUE_AS_OBJECT(*(sp - count))->type != ValueType::String) {
            error("invalid typecast");
            return false;
        }
        Value val = *(sp - count);
        sp -= count + 1;
        try {
            *sp++ = SRCLANG_VALUE_DECIMAL(
                    stod((char *) (SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
        } catch (...) {
            error("invalid typecast");
            return false;
        }
        return true;
    }

    bool call_typecast_char(uint8_t count) {
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

    bool call_typecast_string(uint8_t count) {
        string buf;
        for (auto i = sp - count; i != sp; i++) {
            buf += SRCLANG_VALUE_GET_STRING(*i);
        }
        sp -= count + 1;
        *sp++ = SRCLANG_VALUE_STRING(strdup(buf.c_str()));
        return true;
    }

    bool call_typecast_error(uint8_t count) {
        string buf;
        for (auto i = sp - count; i != sp; i++) {
            buf += SRCLANG_VALUE_GET_STRING(*i);
        }
        sp -= count + 1;
        *sp++ = SRCLANG_VALUE_ERROR(strdup(buf.c_str()));
        return true;
    }

    bool call_typecast_bool(uint8_t count) {
        if (count != 1) {
            error("invalid typecast");
            return false;
        }
        Value val = *(sp - count);
        sp -= count + 1;
        *sp++ = SRCLANG_VALUE_BOOL(!is_falsy(val));
        return true;
    }

    bool call_typecast_type(uint8_t count) {
        if (count != 1) {
            error("invalid typecast");
            return false;
        }
        Value val = *(sp - count);
        sp -= count + 1;
        *sp++ = SRCLANG_VALUE_TYPE(SRCLANG_VALUE_GET_TYPE(val));
        return true;
    }

    bool call_typecast(Value callee, uint8_t count) {
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

    bool call_native(Value callee, uint8_t count) {
        auto native = reinterpret_cast<NativeFunction *>(
                SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        if (native->param.size() != count) {
            error("Expected '" + to_string(native->param.size()) + "' but '" +
                  to_string(count) + "' provided");
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
                error("ERROR: invalid " + to_string(j) + "th parameter, expected '" +
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
                result_value = SRCLANG_VALUE_POINTER((void *) result);
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

    bool call_bounded(Value callee, uint8_t count) {
        auto bounded = (BoundedValue *) SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
        *(sp - count - 1) = bounded->value;
        for (auto i = sp; i != sp - count; i--) {
            *i = *(i - 1);
        }
        *(sp - count) = bounded->parent;
        sp++;
        if (debug) {
            cout << endl;
            print_stack();
        }

        return call(count + 1);
    }

    bool get_property(ValueType type, string id, Value container,
                      Value &value) {
        if (properites[type].find(id) == properites[type].end()) {
            error("no property '" + string(id) + "' for type '" +
                  SRCLANG_VALUE_TYPE_ID[int(type)] + "'");
            return false;
        }
        value = properites[type][id];
        switch (SRCLANG_VALUE_GET_TYPE(value)) {
            case ValueType::Function:
            case ValueType::Builtin:
            case ValueType::Native:
            case ValueType::Bounded: {
                auto bounded_value = new BoundedValue{container, value};
                value = SRCLANG_VALUE_HEAP_OBJECT(
                        ValueType::Bounded, bounded_value);
            }
                break;
            default:
                break;
        }
        return true;
    }

    bool call(uint8_t count) {
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

    bool run() {
        while (true) {
            if (debug) {
                if (debug_info.size()) {
                    cout << debug_info.back()->filename << ":"
                         << debug_info.back()->lines[distance(
                                 cur()->closure->fun->instructions->begin(), cur()->ip)]
                         << endl;
                }

                cout << "  ";
                for (auto i = stack.begin(); i != sp; i++) {
                    cout << "[" << SRCLANG_VALUE_DEBUG(*i) << "] ";
                }
                cout << endl;
                cout << ">> ";
                ByteCode<Byte, Value>::debug(
                        *cur()->closure->fun->instructions.get(), constants,
                        distance(cur()->closure->fun->instructions->begin(), ip()), cout);
                cout << endl;

                if (break_) cin.get();
            }
            auto inst = static_cast<OpCode>(*ip()++);
            switch (inst) {
                case OpCode::CONST:
                    *sp++ = constants[*ip()++];
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
                            if (pos >= globals.size()) {
                                error("GLOBALS SYMBOLS OVERFLOW");
                                return false;
                            }
                            globals[pos] = *(sp - 1);
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
                            *sp++ = globals[pos];
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

                    auto constant = constants[funIndex];
                    auto fun = (Function<Byte> *) SRCLANG_VALUE_AS_OBJECT(constant)->pointer;
                    auto frees = vector<Value>(sp - nfree, sp);
                    sp -= nfree;
                    auto closure = new Closure<Byte, Value>{fun, frees};
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
                    auto list = new vector<Value>(sp - size, sp);
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
                                            to_string(index) +
                                            "' from string of size '" +
                                            to_string(strlen(buffer)) + "'");
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
                        vector<Value> list =
                                *(vector<Value> *) SRCLANG_VALUE_AS_OBJECT(container)
                                        ->pointer;

                        int index = SRCLANG_VALUE_AS_INTEGER(pos);
                        switch (count) {
                            case 1:
                                if (list.size() <= index || index < 0) {
                                    error("out-of-index trying to access '" +
                                          to_string(index) +
                                          "' from list of size '" +
                                          to_string(list.size()) + "'");
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
                            auto res = SRCLANG_VALUE_NULL;
                            if (!get_property(ValueType::Map, buf, container,
                                              res)) {
                                return false;
                            }
                            *sp++ = res;
                        } else {
                            *sp++ = idx->second;
                        }
                    } else {
                        if (SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                            auto property =
                                    (char *) (SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                            auto type_id = SRCLANG_VALUE_GET_TYPE(container);
                            Value value = SRCLANG_VALUE_NULL;
                            if (!get_property(type_id, property, container,
                                              value)) {
                                return false;
                            }
                            *sp++ = value;
                        } else {
                            error("invalid index operation");
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

                case OpCode::IMPL: {
                    auto ty = *--sp;
                    auto val = *--sp;
                    auto id = *--sp;
                    if (!SRCLANG_VALUE_IS_TYPE(ty) ||
                        !(SRCLANG_VALUE_IS_OBJECT(id) &&
                          SRCLANG_VALUE_AS_OBJECT(id)->type ==
                          ValueType::String)) {
                        error("Invalid implementation");
                        return false;
                    }
                    char *property_id = reinterpret_cast<char *>(
                            SRCLANG_VALUE_AS_OBJECT(id)->pointer);
                    properites[SRCLANG_VALUE_AS_TYPE(ty)][property_id] = val;
                }
                    break;

                case OpCode::RET: {
                    auto value = *--sp;
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

                case OpCode::CONTINUE:
                case OpCode::BREAK:
                case OpCode::JMP: {
                    ip() = (cur()->closure->fun->instructions->begin() + *ip());
                }
                    break;

                case OpCode::HLT: {
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
};

SRCLANG_BUILTIN(gc) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    interpreter->gc();
    return SRCLANG_VALUE_TRUE;
}

SRCLANG_BUILTIN(println) {
    for (auto const &i: args) {
        cout << SRCLANG_VALUE_GET_STRING(i);
    }
    cout << '\n';
    return SRCLANG_VALUE_INTEGER(args.size());
}

SRCLANG_BUILTIN(len) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    int length;
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::String:
            return SRCLANG_VALUE_INTEGER(
                    strlen((char *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
        case ValueType::List:
            return SRCLANG_VALUE_INTEGER(
                    ((vector<Value> *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer)
                            ->size());
        default:
            return SRCLANG_VALUE_INTEGER(sizeof(Value));
    }
}

SRCLANG_BUILTIN(append) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->push_back(args[1]);
            return SRCLANG_VALUE_LIST(list);
        }
            break;
        case ValueType::String: {
            auto str = (char *) (SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            switch (SRCLANG_VALUE_GET_TYPE(args[1])) {
                case ValueType::Char: {
                    str = (char *) realloc(str, len + 2);
                    str[len] = SRCLANG_VALUE_AS_CHAR(args[1]);
                    str[len + 1] = '\0';
                }
                    break;
                case ValueType::String: {
                    auto str2 =
                            (char *) (SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    auto len2 = strlen(str2);
                    str = (char *) realloc(str, len + len2 + 1);
                    strcat(str, str2);
                }
                    break;
                default:
                    throw runtime_error("invalid append operation");
            }
            return SRCLANG_VALUE_STRING(str);
        }
            break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                    ("invalid append operation on '" +
                     SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                     "'")
                            .c_str()));
    }
}

SRCLANG_BUILTIN(range) {
    if (args.size() == 1 && SRCLANG_VALUE_GET_TYPE(args[0]) == ValueType::Map) {
        auto map = reinterpret_cast<SrcLangMap *>(
                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
        auto keys = new SrcLangList();
        for (auto i = map->begin(); i != map->end(); i++) {
            auto v = SRCLANG_VALUE_STRING(strdup(i->first.c_str()));
            keys->push_back(v);
        }
        auto list = SRCLANG_VALUE_LIST(keys);
        return list;
    }
    SRCLANG_CHECK_ARGS_RANGE(1, 3);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Integer);
    int start = 0;
    int inc = 1;
    int end = SRCLANG_VALUE_AS_INTEGER(args[0]);

    if (args.size() == 2) {
        start = end;
        end = SRCLANG_VALUE_AS_INTEGER(args[1]);
    } else if (args.size() == 3) {
        start = end;
        end = SRCLANG_VALUE_AS_INTEGER(args[1]);
        inc = SRCLANG_VALUE_AS_INTEGER(args[2]);
    }

    if (inc < 0) {
        auto t = start;
        start = end;
        end = start;
        inc = -inc;
    }

    SrcLangList *list = new SrcLangList();

    for (int i = start; i < end; i += inc) {
        list->push_back(SRCLANG_VALUE_INTEGER(i));
    }
    return SRCLANG_VALUE_LIST(list);
}

SRCLANG_BUILTIN(pop) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->pop_back();
            return SRCLANG_VALUE_LIST(list);
        }
            break;
        case ValueType::String: {
            auto str = (char *) (SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            str[len - 1] = '\0';
            return SRCLANG_VALUE_STRING(str);
        }
            break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                    ("invalid pop operation on '" +
                     SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                     "'")
                            .c_str()));
    }
}

SRCLANG_BUILTIN(bind) {
    SRCLANG_CHECK_ARGS_EXACT(3);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Type);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
    interpreter->properites[SRCLANG_VALUE_AS_TYPE(args[0])]
    [(char *) SRCLANG_VALUE_AS_OBJECT(args[1])->pointer] =
            args[2];
    return SRCLANG_VALUE_NULL;
}

SRCLANG_BUILTIN(clone) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
        switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
            case ValueType::String: {
                return SRCLANG_VALUE_STRING(
                        strdup((char *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
            };
            case ValueType::List: {
                SrcLangList *list = reinterpret_cast<SrcLangList *>(
                        SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
                SrcLangList *new_list =
                        new SrcLangList(list->begin(), list->end());
                return SRCLANG_VALUE_LIST(new_list);
            };
            default:
                if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
                    return SRCLANG_VALUE_ERROR(
                            strdup(("invalid clone operation on '" +
                                    SRCLANG_VALUE_TYPE_ID[int(
                                            SRCLANG_VALUE_GET_TYPE(args[0]))] +
                                    "'")
                                           .c_str()));
                }
        }
    }
    return args[0];
}

SRCLANG_BUILTIN(lower) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = ::tolower(buffer[i]);
    }
    return SRCLANG_VALUE_STRING(newBuffer);
}

SRCLANG_BUILTIN(upper) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = ::toupper(buffer[i]);
    }
    return SRCLANG_VALUE_STRING(newBuffer);
}

SRCLANG_BUILTIN(search) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    auto containerType = SRCLANG_VALUE_GET_TYPE(args[0]);
    auto valueType = SRCLANG_VALUE_GET_TYPE(args[1]);

    switch (containerType) {
        case ValueType::String: {
            char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
            switch (valueType) {
                case ValueType::Char: {
                    char val = SRCLANG_VALUE_AS_CHAR(args[1]);
                    for (int i = 0; i < strlen(buffer); i++) {
                        if (buffer[i] == val) {
                            return SRCLANG_VALUE_INTEGER(i);
                        }
                    }
                    return SRCLANG_VALUE_INTEGER(-1);
                }
                    break;

                case ValueType::String: {
                    char *val = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    char *result = strstr(buffer, val);
                    if (result == nullptr) {
                        return SRCLANG_VALUE_INTEGER(-1);
                    }
                    return SRCLANG_VALUE_INTEGER(strlen(buffer) - (result - buffer));
                }
                    break;

                default:
                    throw runtime_error(
                            "can't search value '" + SRCLANG_VALUE_DEBUG(args[1]) + "' in string container");

            }

        }
            break;

        case ValueType::List: {
            SrcLangList *list = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            for (int i = 0; i < list->size(); i++) {
                if (interpreter->isEqual(args[1], list->at(i))) {
                    return SRCLANG_VALUE_INTEGER(i);
                }
            }
            return SRCLANG_VALUE_INTEGER(-1);
        }
            break;

        case ValueType::Map: {
            SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
            auto map = reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            return SRCLANG_VALUE_BOOL(map->find((char *) SRCLANG_VALUE_AS_OBJECT(args[1])->pointer) != map->end());
        }
    }

    return SRCLANG_VALUE_INTEGER(-1);
}

int main(int argc, char **argv) {
    SymbolTable symbol_table;
    Options options = {
            {"VERSION", SRCLANG_VERSION},
            {"GC_HEAP_GROW_FACTOR",   1.0f},
            {"GC_INITIAL_TRIGGER",    200},
            {"SEARCH_PATH",           string(getenv("HOME")) +
                                      "/.local/share/srclang/:/usr/share/srclang:/usr/lib/srclang/"},
            {"LOAD_LIBC",             true},
            {"LIBC",                  "libc.so.6"},
            {"C_LIBRARY",             ""},
            {"C_INCLUDE",             ""},
            {"EXPERIMENTAL_FEATURES", false},
            {"DEBUG",                 false},
            {"BREAK",                 false},
    };

    optional<string> filename;
    MemoryManager memory_manager;
    enum {
        NoFile, SourceFile, BinaryFile
    } fileType = NoFile;

    for (auto const &i: builtins) memory_manager.heap.push_back(i);

    optional<string> dump = {};
    auto args = new SrcLangList();
    for (int i = 1; i < argc; i++) {
        string arg(argv[i]);
        if (arg[0] == '-' && filename == nullopt) {
            arg = arg.substr(1);
            if (arg == "debug")
                get<bool>(options["DEBUG"]) = true;
            else if (arg == "break")
                get<bool>(options["BREAK"]) = true;
            else if (arg == "dump")
                dump = argv[++i];
            else if (arg.starts_with("search-path=")) {
                get<string>(options["SEARCH_PATH"])
                        = arg.substr(arg.find_first_of('=') + 1)
                          + ":"
                          + get<string>(options["SEARCH_PATH"]);
            } else {
                cerr << "ERROR: unknown flag '-" << arg << "'" << endl;
                return 1;
            }
        } else if (filename == nullopt && filesystem::exists(arg)) {
            filename = absolute(filesystem::path(arg));
            fileType = (filesystem::path(arg).has_extension() &&
                        filesystem::path(arg).extension() == ".src") ?
                       SourceFile :
                       BinaryFile;
        } else {
            args->push_back(SRCLANG_VALUE_STRING(strdup(argv[i])));
        }
    }

    auto args_value = SRCLANG_VALUE_LIST(args);

    string input;
    if (!filename.has_value()) {
        cout << LOGO << endl;
        cout << " * VERSION       : " << SRCLANG_VERSION << endl;
        cout << " * DOCUMENTATION : https://srclang.rlxos.dev/" << endl;
        cout << endl;
        return 0;
    }
    vector<Value> globals(65536);
    {
        int i = 0;
#define X(id) symbol_table.define(#id, i++);
        SRCLANG_BUILTIN_LIST
#undef X
    }

    auto true_symbol = symbol_table.define("true");
    auto false_symbol = symbol_table.define("false");
    auto null_symbol = symbol_table.define("null");
    auto args_symbol = symbol_table.define("__ARGS__");
    auto file_symbol = symbol_table.define("__FILE__");
    auto sys_symbol = symbol_table.define("__PLATFORM__");
    globals[true_symbol.index] = SRCLANG_VALUE_TRUE;
    globals[false_symbol.index] = SRCLANG_VALUE_FALSE;
    globals[null_symbol.index] = SRCLANG_VALUE_NULL;
    globals[args_symbol.index] = args_value;
    vector<Value> constants;

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
    globals[sys_symbol.index] = SRCLANG_VALUE_STRING(strdup("WINDOWS"));
#elif defined(unix) || defined(__unix) || defined(__unix__)
    globals[sys_symbol.index] = SRCLANG_VALUE_STRING(strdup("UNIX"));
#elif defined(__APPLE__) || defined(__MACH__)
                                                                                                                            globals[sys_symbol.index] = SRCLANG_VALUE_STRING(strdup("APPLE"));
#else
    globals[sys_symbol.index] = SRCLANG_VALUE_STRING(strdup("UNKNOWN"));
#endif


    ByteCode<Byte, Value> code;
    shared_ptr<DebugInfo> debug_info{nullptr};

    if (fileType == SourceFile) {
        ifstream reader(*filename);
        input = string((istreambuf_iterator<char>(reader)),
                       (istreambuf_iterator<char>()));
        reader.close();
        Compiler<Iterator, Byte> compiler(
                input.begin(), input.end(),
                (filename == nullopt ? "<script>" : *filename), &symbol_table,
                constants, &memory_manager, options);

        if (!compiler.compile()) {
            return 1;
        }

        if (get<bool>(options["LOAD_LIBC"])) {
            if (dlopen(get<string>(options["LIBC"]).c_str(),
                       RTLD_GLOBAL | RTLD_NOW) == nullptr) {
                cerr << "FAILED to load libc '" << dlerror() << "'" << endl;
                return 1;
            }
        }
        code = std::move(compiler.code());
        debug_info = compiler.global_debug_info;
    } else if (fileType == BinaryFile) {
        ifstream reader(*filename);
        char magic_code[7];
        reader.read(magic_code, 7);
        if (strncmp(magic_code, MAGIC_CODE, 7) != 0) {
            cerr << "MAGIC_CODE mismatch" << endl;
            return 1;
        }
        code = SRCLANG_BYTECODE_READ(reader);
        debug_info = SRCLANG_DEBUGINFO_READ(reader);
    }

    if (get<bool>(options["DEBUG"])) {
        cout << code;
        cout << "== GLOBALS ==" << endl;
        for (auto const &i: symbol_table.store) {
            if (i.second.scope == Symbol::GLOBAL) {
                cout << "- " << i.first << " := "
                     << SRCLANG_VALUE_GET_STRING(globals[i.second.index])
                     << endl;
            }
        }
    }

    if (dump.has_value()) {
        ofstream writer(*dump);
        writer.write(MAGIC_CODE, 7);
        SRCLANG_BYTECODE_DUMP(code, writer);
        SRCLANG_DEBUGINFO_DUMP(debug_info.get(), writer);
        return 0;
    }

    Interpreter<Byte> interpreter(code, globals, debug_info,
                                  &memory_manager, options);
    if (!interpreter.run()) {
        return 1;
    }
    return 0;
}
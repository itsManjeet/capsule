#include <dlfcn.h>
#include <ffi.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
using namespace std;

#define SRCLANG_VERSION 20230221

using Iterator = string::iterator;
using Byte = uint8_t;

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

template <typename Iterator>
struct Token {
    TokenType type;
    string literal;
    Iterator pos;

    friend ostream& operator<<(ostream& os, const Token token) {
        os << SRCLANG_TOKEN_ID[static_cast<int>(token.type)] << ":"
           << token.literal;
        return os;
    }
};

#define SRCLANG_OPCODE_LIST \
    X(CONST)                \
    X(CONST_NULL)           \
    X(LOAD)                 \
    X(STORE)                \
    X(ADD)                  \
    X(SUB)                  \
    X(MUL)                  \
    X(DIV)                  \
    X(NEG)                  \
    X(NOT)                  \
    X(COMMAND)              \
    X(EQ)                   \
    X(NE)                   \
    X(LT)                   \
    X(LE)                   \
    X(GT)                   \
    X(GE)                   \
    X(AND)                  \
    X(OR)                   \
    X(LAND)                 \
    X(LOR)                  \
    X(LSHIFT)               \
    X(RSHIFT)               \
    X(MOD)                  \
    X(BREAK)                \
    X(CONTINUE)             \
    X(FUN)                  \
    X(CALL)                 \
    X(PACK)                 \
    X(INDEX)                \
    X(SET)                  \
    X(POP)                  \
    X(RET)                  \
    X(JNZ)                  \
    X(JMP)                  \
    X(IMPL)                 \
    X(HLT)

enum class OpCode : uint8_t {
#define X(id) id,
    SRCLANG_OPCODE_LIST
#undef X
};

const vector<string> SRCLANG_OPCODE_ID = {
#define X(id) #id,
    SRCLANG_OPCODE_LIST
#undef X
};

#define SRCLANG_SYMBOL_SCOPE_LIST \
    X(BUILTIN)                    \
    X(GLOBAL)                     \
    X(LOCAL)                      \
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
    X(Builtin, "builtin")       \
    X(Native, "native")         \
    X(Error, "error")           \
    X(Bounded, "bounded")       \
    X(Type, "type")

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

#define SRCLANG_VALUE_HEAP_OBJECT(type, ptr, destructor, printer) \
    SRCLANG_VALUE_OBJECT(                                         \
        (new HeapObject{(type), (ptr), (destructor), (printer)}))

#define SRCLANG_VALUE_STRING(str)                       \
    SRCLANG_VALUE_HEAP_OBJECT(                          \
        ValueType::String, (void*)str, ([](void* ptr) { \
            if (ptr != nullptr) delete[] (char*)ptr;    \
        }),                                             \
        ([](void* ptr) -> string { return (char*)ptr; }))

#define SRCLANG_VALUE_LIST(list)                                              \
    SRCLANG_VALUE_HEAP_OBJECT(ValueType::List, (void*)list, ([](void* ptr) {  \
                                  if (ptr != nullptr) delete[] (char*)ptr;    \
                              }),                                             \
                              ([](void* ptr) -> string {                      \
                                  stringstream ss;                            \
                                  vector<Value> list =                        \
                                      *reinterpret_cast<vector<Value>*>(ptr); \
                                  ss << "[";                                  \
                                  string sep;                                 \
                                  for (auto i : list) {                       \
                                      ss << sep << srclang_value_print(i);    \
                                      sep = ", ";                             \
                                  }                                           \
                                  ss << "]";                                  \
                                  return ss.str();                            \
                              }))

#define SRCLANG_VALUE_ERROR(err)                  \
    SRCLANG_VALUE_HEAP_OBJECT(                    \
        ValueType::Error, (void*)err,             \
        ([](void* ptr) { delete[] (char*)ptr; }), \
        ([](void* ptr) -> string { return (char*)ptr; }))

#define SRCLANG_VALUE_NATIVE(native)                                          \
    SRCLANG_VALUE_HEAP_OBJECT(ValueType::Native, (void*)native,               \
                              ([](void* ptr) {}), ([](void* ptr) -> string {  \
                                  NativeFunction* native =                    \
                                      reinterpret_cast<NativeFunction*>(ptr); \
                                  return "<native(" + native->id + ")";       \
                              }))

#define SRCLANG_VALUE_BUILTIN(id)                                    \
    SRCLANG_VALUE_HEAP_OBJECT(                                       \
        ValueType::Builtin, (void*)builtin_##id, ([](void* ptr) {}), \
        ([](void* ptr) -> string { return "<builtin " #id ">"; }))

#define SRCLANG_VALUE_FUNCTION(fun)                                           \
    SRCLANG_VALUE_HEAP_OBJECT(                                                \
        ValueType::Function, (void*)fun, ([](void* ptr) {                     \
            delete reinterpret_cast<Function<Byte, Value>*>(ptr);             \
        }),                                                                   \
        ([](void* ptr) -> string {                                            \
            Function<Byte, Value>* fun =                                      \
                reinterpret_cast<Function<Byte, Value>*>(ptr);                \
            vector<Value> constants;                                          \
            stringstream ss;                                                  \
            for (int offset = 0; offset < fun->instructions->size();) {       \
                offset = ByteCode<Byte, Value>::debug(*fun->instructions,     \
                                                      constants, offset, ss); \
                ss << " | ";                                                  \
            }                                                                 \
            return ss.str();                                                  \
        }))

#define SRCLANG_VALUE_TRUE \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_TRUE))
#define SRCLANG_VALUE_FALSE \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_FALSE))
#define SRCLANG_VALUE_NULL \
    ((Value)(uint64_t)(SRCLANG_VALUE_QNAN | SRCLANG_VALUE_TAG_NULL))

#define SRCLANG_BUILTIN(id) \
    Value builtin_##id(vector<Value>& args, Interpreter<Byte>* interpreter)

#define SRCLANG_VALUE_GET_TYPE(val) srclang_value_get_type(val)
#define SRCLANG_VALUE_GET_STRING(val) srclang_value_print(val)

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

typedef vector<Value> SrcLangList;
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
    using Destructor = function<void(void*)>;
    using Printer = function<string(void*)>;

    ValueType type;
    void* pointer{nullptr};

    Destructor destructor{nullptr};
    Printer printer{nullptr};

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

ValueType srclang_value_get_type(Value val) {
    if (SRCLANG_VALUE_IS_NULL(val)) return ValueType::Null;
    if (SRCLANG_VALUE_IS_BOOL(val)) return ValueType::Boolean;
    if (SRCLANG_VALUE_IS_DECIMAL(val)) return ValueType::Decimal;
    if (SRCLANG_VALUE_IS_CHAR(val)) return ValueType::Char;
    if (SRCLANG_VALUE_IS_TYPE(val)) return ValueType::Type;
    if (SRCLANG_VALUE_IS_INTEGER(val)) return ValueType::Integer;

    if (SRCLANG_VALUE_IS_OBJECT(val))
        return (SRCLANG_VALUE_AS_OBJECT(val)->type);
    throw runtime_error("invalid value '" + to_string((uint64_t)val) + "'");
}

string srclang_value_print(Value val) {
    auto type = srclang_value_get_type(val);
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
            return string(1, (char)SRCLANG_VALUE_AS_CHAR(val));
        case ValueType::Type:
            return "<type(" +
                   SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_AS_TYPE(val))] +
                   ")>";
        default:
            return SRCLANG_VALUE_AS_OBJECT(val)->printer(
                (SRCLANG_VALUE_AS_OBJECT(val)->pointer));
    }
}

struct SymbolTable {
    SymbolTable* parent{nullptr};
    map<string, Symbol> store;

    Symbol define(string name) {
        store[name] =
            Symbol{name, (parent == nullptr ? Symbol::GLOBAL : Symbol::LOCAL),
                   (int)store.size()};
        return store[name];
    }

    Symbol define(string name, int index) {
        store[name] = Symbol{name, Symbol::BUILTIN, index};
        return store[name];
    }

    optional<Symbol> resolve(string name) {
        auto iter = store.find(name);
        if (iter != store.end()) {
            return iter->second;
        }
        if (parent != nullptr) {
            return parent->resolve(name);
        }
        return nullopt;
    }
};

struct MemoryManager {
    using Heap = vector<Value>;
    Heap heap;

    MemoryManager() = default;

    ~MemoryManager() {
        for (auto i = heap.begin(); i != heap.end(); i++) {
            if (SRCLANG_VALUE_IS_OBJECT(*i)) {
                auto obj = SRCLANG_VALUE_AS_OBJECT(*i);
                obj->destructor(obj->pointer);
                delete obj;
            }
        }
    }

    void mark(Heap::iterator start, Heap::iterator end) {
        for (auto i = start; i != end; i++) {
            if (SRCLANG_VALUE_IS_OBJECT(*i)) {
                auto obj = SRCLANG_VALUE_AS_OBJECT(*i);
                obj->marked = true;
#ifdef SRCLANG_GC_DEBUG
                cout << "  marked "
                     << uintptr_t(SRCLANG_VALUE_AS_OBJECT(*i)->pointer) << endl;
#endif
                if (obj->type == ValueType::List) {
                    mark(
                        reinterpret_cast<vector<Value>*>(obj->pointer)->begin(),
                        reinterpret_cast<vector<Value>*>(obj->pointer)->end());
                }
            }
        }
    }

    void sweep() {
        for (auto i = heap.begin(); i != heap.end(); i++) {
            if (SRCLANG_VALUE_IS_OBJECT(*i)) {
                auto obj = SRCLANG_VALUE_AS_OBJECT(*i);
                if (obj->marked) {
                    obj->marked = false;
                } else {
#ifdef SRCLANG_GC_DEBUG
                    cout << "   deallocating " << uintptr_t(obj->pointer)
                         << endl;
#endif
                    obj->destructor(obj->pointer);
                    delete obj;
                    heap.erase(i);
                }
            }
        }
    }
};

struct DebugInfo {
    string filename;
    vector<int> lines;
    int position;
};

template <typename Byte>
struct Instructions : vector<Byte> {
    Instructions() = default;

    OpCode last_instruction;

    size_t emit(DebugInfo* debug_info, int line) { return 0; }

    template <typename T, typename... Types>
    size_t emit(DebugInfo* debug_info, int line, T byte, Types... operand) {
        size_t pos = this->size();
        this->push_back(static_cast<Byte>(byte));
        debug_info->lines.push_back(line);
        emit(debug_info, line, operand...);
        last_instruction = OpCode(byte);
        return pos;
    }
};

template <typename Byte, typename Constant>
struct ByteCode {
    unique_ptr<Instructions<Byte>> instructions;
    vector<Constant> constants;
    using Iterator = typename vector<Constant>::iterator;

    static int debug(Instructions<Byte> const& instructions,
                     vector<Constant> const& constants, int offset,
                     ostream& os) {
        os << setfill('0') << setw(4) << offset << " ";
        auto op = static_cast<OpCode>(instructions[offset]);
        os << SRCLANG_OPCODE_ID[static_cast<int>(op)];
        offset += 1;
        switch (op) {
            case OpCode::CONST: {
                int pos = instructions[offset++];
                if (constants.size() > 0) {
                    os << " " << pos << " '"
                       << srclang_value_print(constants[pos]) << "'";
                }

            } break;
            case OpCode::INDEX:
            case OpCode::PACK: {
                os << " " << (int)instructions[offset++];
            } break;
            case OpCode::CONTINUE:
            case OpCode::BREAK:
            case OpCode::JNZ:
            case OpCode::JMP: {
                int pos = instructions[offset++];
                os << " '" << pos << "'";
            } break;
            case OpCode::LOAD:
            case OpCode::STORE: {
                int scope = instructions[offset++];
                int pos = instructions[offset++];
                os << " " << pos << " '" << SRCLANG_SYMBOL_ID[scope] << "'";
            } break;
            case OpCode::CALL: {
                int count = instructions[offset++];
                os << " '" << count << "'";
            } break;
            default:
                break;
        }

        return offset;
    }

    friend ostream& operator<<(ostream& os,
                               const ByteCode<Byte, Constant>& bytecode) {
        os << "== CODE ==" << endl;
        for (int offset = 0; offset < bytecode.instructions->size();) {
            offset = ByteCode<Byte, Constant>::debug(
                *bytecode.instructions, bytecode.constants, offset, os);
            os << endl;
        }
        os << "\n== CONSTANTS ==" << endl;
        for (auto i = 0; i < bytecode.constants.size(); i++) {
            os << i << " " << srclang_value_print(bytecode.constants[i])
               << endl;
        }
        return os;
    }
};

enum class FunctionType { Function, Method, Initializer, Native };
template <typename Byte, typename Constant>
struct Function {
    FunctionType type;
    unique_ptr<Instructions<Byte>> instructions;
    int nlocals;
    int nparams;
    shared_ptr<DebugInfo> debug_info;
};

template <typename Byte>
struct Interpreter;

typedef Value (*Builtin)(vector<Value>&, Interpreter<Byte>*);

template <class V>
type_info const& variant_typeid(V const& v) {
    return visit([](auto&& x) -> decltype(auto) { return typeid(x); }, v);
}

template <typename Iterator, typename Byte>
struct Compiler {
    SymbolTable* symbol_table{nullptr};
    MemoryManager* memory_manager;
    SymbolTable* global;
    Token<Iterator> cur, peek;
    Iterator iter, start, end;
    string filename;
    vector<Value>& constants;
    vector<string> loaded_imports;
    vector<unique_ptr<Instructions<Byte>>> instructions;
    using OptionType = variant<string, double, bool>;
    DebugInfo* debug_info;
    shared_ptr<DebugInfo> global_debug_info;
    map<string, OptionType> options = {
        {"VERSION", double(SRCLANG_VERSION)},
        {"GC_HEAP_GROW_FACTOR", double(2)},
        {"GC_INITIAL_TRIGGER", double(30)},
        {"SEARCH_PATH", string("/usr/lib/srclang/")},
        {"LOAD_LIBC", true},
        {"LIBC", "libc.so.6"},
    };

    Compiler(Iterator start, Iterator end, string filename, SymbolTable* global,
             vector<Value>& constants, MemoryManager* memory_manager)
        : iter{start},
          start{start},
          end{end},
          constants{constants},
          global{global},
          symbol_table{global},
          filename{filename},
          memory_manager{memory_manager} {
        global_debug_info = make_shared<DebugInfo>();
        global_debug_info->filename = filename;
        global_debug_info->position = 0;
        debug_info = global_debug_info.get();
        instructions.push_back(make_unique<Instructions<Byte>>());
        eat();
        eat();
    }

    ByteCode<Byte, Value> code() {
        return ByteCode<Byte, Value>{move(instructions.back()), constants};
    }

    Instructions<Byte>* inst() { return instructions.back().get(); }

    void push_scope() {
        symbol_table = new SymbolTable{symbol_table};
        instructions.push_back(make_unique<Instructions<Byte>>());
    }

    unique_ptr<Instructions<Byte>> pop_scope() {
        auto old = symbol_table;
        symbol_table = symbol_table->parent;
        delete old;
        auto res = move(instructions.back());
        instructions.pop_back();
        return move(res);
    }

    template <typename Message>
    void error(const Message& mesg, Iterator pos) const {
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

    Iterator get_error_pos(Iterator err_pos, int& line) const {
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

    string get_error_line(Iterator err_pos) const {
        Iterator i = err_pos;
        // position i to the next EOL
        while (i != end && (*i != '\r' && *i != '\n')) ++i;
        return string(err_pos, i);
    }

    bool consume(const string& expected) {
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
    bool expect(const string& expected) {
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

        auto escape = [&](Iterator& iterator, bool& status) -> char {
            if (*iterator == '\\') {
                char ch = *++iterator;
                iterator++;
                switch (ch) {
                    case 'a':
                        return '\a';
                    case 'b':
                        return '\b';
                    case 'e':
                        return '\e';
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
        for (string k : {"let", "fun", "native", "return", "if", "else", "for",
                         "break", "continue", "import", "global", "impl",

                         // specical operators
                         "#!", "not",

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

    enum Precedence : int {
        P_None,
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
    int precedence(string tok) {
        static map<string, int> _prec = {
            {":=", P_Assignment}, {"=", P_Assignment},  {"or", P_Or},
            {"and", P_And},       {"&", P_Land},        {"|", P_Lor},
            {"==", P_Equality},   {"!=", P_Equality},   {">", P_Comparison},
            {"<", P_Comparison},  {">=", P_Comparison}, {"<=", P_Comparison},
            {">>", P_Shift},      {"<<", P_Shift},      {"+", P_Term},
            {"-", P_Term},        {"*", P_Factor},      {"/", P_Factor},
            {"%", P_Factor},      {"not", P_Unary},     {"-", P_Unary},
            {"$", P_Unary},       {".", P_Call},        {"[", P_Call},
            {"(", P_Call},
        };
        auto i = _prec.find(tok);
        if (i == _prec.end()) {
            return -1;
        }
        return i->second;
    }

    int emit() { return 0; }

    template <typename T, typename... Ts>
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
        for (auto i : cur.literal) {
            if (i == '.') {
                if (is_float) {
                    error("multiple floating point detected", cur.pos);
                    return false;
                }
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
        } catch (std::invalid_argument const& e) {
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
    bool function() {
        auto pos = cur.pos;
        push_scope();
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

        int nlocals = symbol_table->store.size();
        auto fun_instructions = pop_scope();
        if (fun_instructions->last_instruction == OpCode::POP) {
            fun_instructions->pop_back();
            fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
        } else if (fun_instructions->last_instruction != OpCode::RET) {
            fun_instructions->emit(fun_debug_info.get(), line,
                                   OpCode::CONST_NULL);
            fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
        }

        auto fun = new Function<Byte, Value>{FunctionType::Function,
                                             move(fun_instructions), nlocals,
                                             nparam, fun_debug_info};
        auto fun_value = SRCLANG_VALUE_FUNCTION(fun);
        memory_manager->heap.push_back(fun_value);
        constants.push_back(fun_value);

        emit(OpCode::CONST, constants.size() - 1);
        emit(OpCode::FUN);
        return true;
    }

    /// impl ::= 'impl' <identifier> 'for' <type>
    bool impl() {
        if (!check(TokenType::Identifier)) return false;
        auto id = cur.literal;
        constants.push_back(SRCLANG_VALUE_STRING(strdup(id.c_str())));
        emit(OpCode::CONST, constants.size() - 1);
        if (!identifier(false)) return false;

        if (!expect("for")) return false;
        ValueType ty;

        try {
            ty = type(cur.literal);
            emit(OpCode::LOAD, Symbol::Scope::TYPE, int(ty));
        } catch (exception const& exc) {
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
        } else if (consume("fun")) {
            return function();
        } else if (consume("[")) {
            return list();
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
        int count = 0;
        while (!consume(")")) {
            count++;
            if (!expression()) {
                return false;
            }
            if (consume(")")) break;
            if (!consume(",")) return false;
        }
        emit(OpCode::CALL, count);
        return true;
    }

    /// index ::= <expression> '[' <expession> ']'
    bool index(bool can_assign) {
        if (!expression()) return false;
        if (!expect("]")) return false;
        if (can_assign && consume("=")) {
            if (!expression()) return false;
            emit(OpCode::SET);
        } else {
            emit(OpCode::INDEX, 0);
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
            if (!expression()) return false;
            emit(OpCode::SET);
        } else {
            emit(OpCode::INDEX, 0);
        }

        return true;
    }

    bool infix(bool can_assign) {
        static map<string, OpCode> binop = {
            {"+", OpCode::ADD},     {"-", OpCode::SUB},
            {"/", OpCode::DIV},     {"*", OpCode::MUL},
            {"==", OpCode::EQ},     {"!=", OpCode::NE},
            {"<", OpCode::LT},      {">", OpCode::GT},
            {">=", OpCode::GE},     {"<=", OpCode::LE},
            {"and", OpCode::AND},   {"or", OpCode::OR},
            {"|", OpCode::LOR},     {"&", OpCode::LAND},
            {">>", OpCode::LSHIFT}, {"<<", OpCode::RSHIFT},
            {"%", OpCode::MOD},
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
        bool can_assign = prec <= 10;
        if (!prefix(can_assign)) {
            return false;
        }

        while ((cur.literal != ";" && cur.literal != "{") &&
               prec < precedence(cur.literal)) {
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
                case TokenType::Number:
                    CHECK_TYPE_ID(double);
                    value = stod(cur.literal);
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
            if (SRCLANG_VERSION > get<double>(value)) {
                error(
                    "Code need srclang of version above or equal to "
                    "'" +
                        to_string(SRCLANG_VERSION) + "'",
                    pos);
                return false;
            }
        } else if (option_id == "SEARCH_PATH") {
            options[option_id] =
                get<string>(options[option_id]) + (":" + get<string>(value));
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

        if (!expression()) {
            return false;
        }

        emit(OpCode::STORE, symbol->scope, symbol->index);
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
                case OpCode::CONST: {
                    i++;
                } break;
                case OpCode::CONTINUE:
                case OpCode::BREAK: {
                    if (j == to_patch) inst()->at(i++) = pos;
                } break;
                case OpCode::JNZ:
                case OpCode::JMP: {
                    i++;
                } break;
                case OpCode::LOAD:
                case OpCode::STORE: {
                    i += 2;
                } break;
                case OpCode::CALL: {
                    i++;
                } break;
                default:
                    break;
            }
        }
    }

    /// loop ::= 'for' <expression> <block>
    bool loop() {
        auto loop_start = inst()->size();
        if (!expression()) return false;

        auto loop_exit = emit(OpCode::JNZ, 0);
        if (!block()) return false;

        patch_loop(loop_start, OpCode::CONTINUE, loop_start);

        emit(OpCode::JMP, loop_start);
        constants.push_back(SRCLANG_VALUE_NULL);
        auto after_condition = emit(OpCode::CONST, (int)constants.size() - 1);

        inst()->at(loop_exit + 1) = after_condition;
        patch_loop(loop_start, OpCode::BREAK, after_condition);
        return true;
    }

    /// import ::= 'import' <string>
    bool import_() {
        if (!check(TokenType::String)) return false;
        auto path = cur.literal;
        int line;
        get_error_pos(cur.pos, line);

        stringstream ss(get<string>(options["SEARCH_PATH"]));
        string search_path;
        bool found = false;
        while (getline(ss, search_path, ':')) {
            search_path = search_path + "/" + path + ".src";
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

        Compiler<Iterator, Byte> compiler(input.begin(), input.end(),
                                          search_path, symbol_table, constants,
                                          memory_manager);

        if (!compiler.compile()) {
            error("failed to import '" + search_path + "'", cur.pos);
            return false;
        }
        auto instructions = move(compiler.code().instructions);
        instructions->pop_back();  // pop OpCode::HLT
        if (instructions->last_instruction == OpCode::POP) {
            instructions->pop_back();
            instructions->emit(compiler.global_debug_info.get(), 0,
                               OpCode::RET);
        } else {
            instructions->emit(compiler.global_debug_info.get(), 0,
                               OpCode::CONST_NULL);
            instructions->emit(compiler.global_debug_info.get(), 0,
                               OpCode::RET);
        }

        auto fun = new Function<Byte, Value>{FunctionType::Function,
                                             move(instructions), 0, 0,
                                             compiler.global_debug_info};
        auto val = SRCLANG_VALUE_FUNCTION(fun);
        memory_manager->heap.push_back(val);
        constants.push_back(val);
        emit(OpCode::CONST, constants.size() - 1);
        emit(OpCode::FUN);
        emit(OpCode::CALL, 0);
        eat();
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

        auto after_alt_pos = inst()->size();
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
    bool native() {
        if (!check(TokenType::Identifier)) return false;
        auto id = cur.literal;
        auto symbol = symbol_table->resolve(id);
        if (symbol) {
            error("variable already defined with name '" + id + "'", cur.pos);
            return false;
        }
        symbol = symbol_table->define(id);
        vector<ValueType> types;
        ValueType ret_type;

        if (!eat()) return false;

        if (!expect("(")) return false;

        while (!consume(")")) {
            if (!check(TokenType::Identifier)) return false;
            try {
                types.push_back(type(cur.literal));
            } catch (runtime_error const& e) {
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
        } catch (runtime_error const& e) {
            error(e.what(), cur.pos);
            return false;
        }

        auto native = new NativeFunction{id, types, ret_type};
        Value val = SRCLANG_VALUE_NATIVE(native);
        memory_manager->heap.push_back(val);
        constants.push_back(val);
        emit(OpCode::CONST, constants.size() - 1);
        emit(OpCode::STORE, symbol->scope, symbol->index);
        return expect(";");
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
        } else if (consume("native")) {
            return native();
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
    X(pop)

template <typename Byte>
struct Interpreter;
#define X(id) SRCLANG_BUILTIN(id);
SRCLANG_BUILTIN_LIST
#undef X

enum Builtins {
#define X(id) BUILTIN_##id,
    SRCLANG_BUILTIN_LIST
#undef X
};

const vector<Value> builtins = {
#define X(id) SRCLANG_VALUE_BUILTIN(id),
    SRCLANG_BUILTIN_LIST
#undef X
};

template <typename Byte>
struct Interpreter {
    struct Frame {
        typename vector<Byte>::iterator ip;
        Function<Byte, Value>* fun;
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
            },
        },

        {
            ValueType::List,
            {
                ADD_BUILTIN_PROPERTY(len),
                ADD_BUILTIN_PROPERTY(append),
                ADD_BUILTIN_PROPERTY(pop),
                ADD_BUILTIN_PROPERTY(clone),
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

    int next_gc = 20;
    int GC_HEAP_GROW_FACTOR = 2;

    vector<Value> stack;
    vector<Value>::iterator sp;
    MemoryManager* memory_manager;

    vector<Value>& globals;
    vector<Value>& constants;
    vector<Frame> frames;
    typename vector<Frame>::iterator fp;
    vector<shared_ptr<DebugInfo>> debug_info;
    bool debug;

    void error(string const& mesg) {
        if (debug_info.size() == 0) {
            cerr << "ERROR: " << mesg << endl;
            return;
        }

        cerr << debug_info.back()->filename << ":"
             << debug_info.back()->lines[distance(
                    cur()->fun->instructions->begin(), cur()->ip)]
             << endl;
        cerr << "  ERROR: " << mesg << endl;
    }

    Interpreter(ByteCode<Byte, Value>& code, vector<Value>& globals,
                shared_ptr<DebugInfo> const& debug_info,
                MemoryManager* memory_manager)
        : stack(1024),
          frames(256),
          globals{globals},
          constants{code.constants},
          memory_manager{memory_manager} {
        sp = stack.begin();
        fp = frames.begin();
        this->debug_info.push_back(debug_info);
        auto fun = new Function<Byte, Value>{
            FunctionType::Function, move(code.instructions), 0, 0, debug_info};
        fp->fun = move(fun);
        fp->ip = fp->fun->instructions->begin();
        fp->bp = sp;
        fp++;
    }

    ~Interpreter() { delete (frames.begin())->fun; }

    void add_object(Value val) {
#ifdef SRCLANG_GC_DEBUG
        gc();
#else
        if (memory_manager->heap.size() > next_gc) {
            gc();
            next_gc = memory_manager->heap.size() * GC_HEAP_GROW_FACTOR;
        }
#endif
        memory_manager->heap.push_back(val);
    }

    typename vector<Byte>::iterator& ip() { return (fp - 1)->ip; }

    typename vector<Frame>::iterator cur() { return (fp - 1); }

    void gc() {
#ifdef SRCLANG_GC_DEBUG
        cout << "Total allocations: " << memory_manager->heap.size() << endl;
        cout << "gc begin:" << endl;
#endif
        memory_manager->mark(stack.begin(), sp);
        memory_manager->mark(globals.begin(), globals.end());
        memory_manager->mark(constants.begin(), constants.end());
        memory_manager->sweep();
#ifdef SRCLANG_GC_DEBUG
        cout << "gc end:" << endl;
        cout << "Total allocations: " << memory_manager->heap.size() << endl;
#endif
    }

    bool unary(Value a, OpCode op) {
        if (OpCode::NOT == op) {
            *sp++ = SRCLANG_VALUE_BOOL(is_falsy(a));
            return true;
        }
        if (srclang_value_get_type(a) == ValueType::Integer) {
            switch (op) {
                case OpCode::NEG:
                    *sp++ = SRCLANG_VALUE_INTEGER(-SRCLANG_VALUE_AS_INTEGER(a));
                    break;
                default:
                    error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(srclang_value_get_type(a))] +
                        "'");
                    return false;
            }
        } else if (srclang_value_get_type(a) == ValueType::Decimal) {
            switch (op) {
                case OpCode::NEG:
                    *sp++ = SRCLANG_VALUE_DECIMAL(-SRCLANG_VALUE_AS_DECIMAL(a));
                    break;
                default:
                    error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(srclang_value_get_type(a))] +
                        "'");
                    return false;
            }
        } else if (srclang_value_get_type(a) == ValueType::String) {
            switch (op) {
                case OpCode::COMMAND: {
                    array<char, 128> buffer;
                    string result;
                    unique_ptr<FILE, decltype(&pclose)> pipe(
                        popen((char*)(SRCLANG_VALUE_AS_OBJECT(a))->pointer,
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
                } break;
                default:
                    error(
                        "unexpected unary operator '" +
                        SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                        SRCLANG_VALUE_TYPE_ID[int(srclang_value_get_type(a))] +
                        "'");
                    return false;
            }
        } else {
            error("ERROR: unhandler unary operation for value of type " +
                  SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                      srclang_value_get_type(a))] +
                  "'");
            return false;
        }

        return true;
    }

    bool binary(Value lhs, Value rhs, OpCode op) {
        if (op == OpCode::NE &&
            SRCLANG_VALUE_GET_TYPE(lhs) != SRCLANG_VALUE_GET_TYPE(rhs)) {
            *sp++ = SRCLANG_VALUE_TRUE;
            return true;
        }

        auto type = SRCLANG_VALUE_GET_TYPE(lhs);

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
                    char* buf = new char[2];
                    buf[0] = a;
                    buf[1] = b;
                    *sp++ = SRCLANG_VALUE_STRING(buf);
                    add_object(*(sp - 1));
                } break;
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
        } else if (srclang_value_get_type(lhs) == ValueType::String) {
            char* a =
                reinterpret_cast<char*>(SRCLANG_VALUE_AS_OBJECT(lhs)->pointer);
            if (SRCLANG_VALUE_GET_TYPE(rhs) != ValueType::String) {
                error("can't apply binary operation '" +
                      SRCLANG_OPCODE_ID[static_cast<int>(op)] + "' for '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(lhs))] +
                      "' and '" +
                      SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(rhs))] +
                      "'");
                return false;
            }
            char* b =
                reinterpret_cast<char*>(SRCLANG_VALUE_AS_OBJECT(rhs)->pointer);

            switch (op) {
                case OpCode::ADD: {
                    int size = strlen(a) + strlen(b) + 1;
                    char* buf = new char[size];
                    snprintf(buf, size, "%s%s", a, b);
                    buf[size] = '\0';
                    auto val = SRCLANG_VALUE_STRING(buf);
                    add_object(val);
                    *sp++ = val;
                } break;
                case OpCode::EQ: {
                    *sp++ = strcmp(a, b) == 0 ? SRCLANG_VALUE_TRUE
                                              : SRCLANG_VALUE_FALSE;
                } break;
                case OpCode::NE: {
                    *sp++ = strcmp(a, b) != 0 ? SRCLANG_VALUE_TRUE
                                              : SRCLANG_VALUE_FALSE;
                } break;
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
                    *sp++ = SRCLANG_VALUE_LIST(c);
                    add_object(*(sp - 1));
                } break;
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
                      srclang_value_get_type(lhs))] +
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

    bool call_function(Value callee, uint8_t count) {
        auto fun = reinterpret_cast<Function<Byte, Value>*>(
            SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        if (count != fun->nparams) {
            cerr << "Invalid parameters" << endl;
            return false;
        }
        fp->fun = move(fun);
        fp->ip = fp->fun->instructions->begin();
        fp->bp = (sp - count);
        sp = fp->bp + fun->nlocals;
        debug_info.push_back(fp->fun->debug_info);
        fp++;
        return true;
    }

    bool call_builtin(Value callee, uint8_t count) {
        auto builtin =
            reinterpret_cast<Builtin>(SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        vector<Value> args(sp - count, sp);
        sp -= count + 1;
        try {
            *sp++ = builtin(args, this);
        } catch (runtime_error const& e) {
            error(e.what());
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
                    stoi((char*)(SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
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
                stod((char*)(SRCLANG_VALUE_AS_OBJECT(val)->pointer)));
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
            buf += srclang_value_print(*i);
        }
        sp -= count + 1;
        *sp++ = SRCLANG_VALUE_STRING(strdup(buf.c_str()));
        return true;
    }

    bool call_typecast_error(uint8_t count) {
        string buf;
        for (auto i = sp - count; i != sp; i++) {
            buf += srclang_value_print(*i);
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
        *sp++ = SRCLANG_VALUE_TYPE(srclang_value_get_type(val));
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
        auto native = reinterpret_cast<NativeFunction*>(
            SRCLANG_VALUE_AS_OBJECT(callee)->pointer);
        if (native->param.size() != count) {
            error("Expected '" + to_string(native->param.size()) + "' but '" +
                  to_string(count) + "' provided");
            return false;
        }
        void* handler = dlsym(nullptr, native->id.c_str());
        if (handler == nullptr) {
            error(dlerror());
            return false;
        }

        ffi_cif cif;
        void* values[count];
        ffi_type* types[count];
        int j = 0;
        for (auto i = sp - count; i != sp; i++, j++) {
            auto type = srclang_value_get_type(*i);
            if (type != native->param[j]) {
                error("ERROR: invalid " + to_string(j) + "th parameter");
                return false;
            }
            switch (type) {
                case ValueType::Null:
                    values[j] = nullptr;
                    types[j] = &ffi_type_pointer;
                    break;
                case ValueType::Integer: {
                    values[j] = &(*i);
                    (*(int64_t*)(values[j])) >>= 3;
                    types[j] = &ffi_type_slong;

                } break;
                case ValueType::Char: {
                    values[j] = &(*i);
                    (*(char*)(values[j])) >>= 3;
                    types[j] = &ffi_type_uint8;
                } break;
                case ValueType::Decimal: {
                    values[j] = &(*i);
                    types[j] = &ffi_type_double;
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
        if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, count, &ffi_type_slong,
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
            case ValueType::Decimal:
                result_value = SRCLANG_VALUE_DECIMAL(result);
                break;
            case ValueType::String:
                result_value = SRCLANG_VALUE_STRING((char*)result);
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
        auto bounded = (BoundedValue*)SRCLANG_VALUE_AS_OBJECT(callee)->pointer;
        *(sp - count - 1) = bounded->value;
        for (auto i = sp; i != sp - count; i--) {
            *i = *(i - 1);
        }
        *(sp - count) = bounded->parent;
        sp++;
        if (debug) {
            cout << endl << "  ";
            for (auto i = stack.begin(); i != sp; i++) {
                cout << "[" << srclang_value_print(*i) << "] ";
            }
            cout << endl;
        }

        return call(count + 1);
    }

    bool call(uint8_t count) {
        auto callee = *(sp - 1 - count);
        if (SRCLANG_VALUE_IS_TYPE(callee)) {
            return call_typecast(callee, count);
        } else if (SRCLANG_VALUE_IS_OBJECT(callee)) {
            switch (SRCLANG_VALUE_AS_OBJECT(callee)->type) {
                case ValueType::Function:
                    return call_function(callee, count);
                case ValueType::Builtin:
                    return call_builtin(callee, count);
                case ValueType::Native:
                    return call_native(callee, count);
                case ValueType::Bounded:
                    return call_bounded(callee, count);
                default:
                    error("ERROR: can't call object of type '" +
                          SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                              srclang_value_get_type(callee))] +
                          "'");
                    return false;
            }
        }
        error("ERROR: can't call value of type '" +
              SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                  srclang_value_get_type(callee))] +
              "'");
        return false;
    }

    bool run() {
        while (true) {
            if (debug) {
                if (debug_info.size()) {
                    cout << debug_info.back()->filename << ":"
                         << debug_info.back()->lines[distance(
                                cur()->fun->instructions->begin(), cur()->ip)]
                         << endl;
                }

                cout << "  ";
                for (auto i = stack.begin(); i != sp; i++) {
                    cout << "[" << srclang_value_print(*i) << "] ";
                }
                cout << endl;
                cout << ">> ";
                ByteCode<Byte, Value>::debug(
                    *cur()->fun->instructions.get(), constants,
                    distance(cur()->fun->instructions->begin(), ip()), cout);
                cout << endl;
                cin.get();
            }
            auto inst = static_cast<OpCode>(*ip()++);
            switch (inst) {
                case OpCode::CONST:
                    *sp++ = constants[*ip()++];
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
                } break;

                case OpCode::COMMAND:
                case OpCode::NOT:
                case OpCode::NEG: {
                    if (!unary(*--sp, inst)) {
                        return false;
                    }
                } break;

                case OpCode::STORE: {
                    auto scope = Symbol::Scope(*ip()++);
                    int pos = *ip()++;

                    switch (scope) {
                        case Symbol::Scope::LOCAL:
                            *(cur()->bp + pos) = *(sp - 1);
                            break;
                        case Symbol::Scope::GLOBAL:
                            globals[pos] = *(sp - 1);
                            break;
                        default:
                            error("Invalid STORE operation on '" +
                                  SRCLANG_SYMBOL_ID[int(scope)] + "'");
                            return false;
                    }
                } break;

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
                        default:
                            error("ERROR: can't load value of scope '" +
                                  SRCLANG_SYMBOL_ID[int(scope)] + "'");
                            return false;
                    }

                } break;

                case OpCode::FUN: {
                } break;

                case OpCode::CALL: {
                    int count = *ip()++;
                    if (!call(count)) {
                        return false;
                    }
                } break;

                case OpCode::POP: {
                    *--sp;
                } break;

                case OpCode::PACK: {
                    auto size = *ip()++;
                    auto list = new vector<Value>(sp - size, sp);
                    sp -= size;
                    auto list_value = SRCLANG_VALUE_LIST(list);
                    add_object(list_value);
                    *sp++ = list_value;
                } break;

                case OpCode::INDEX: {
                    auto count = *ip()++;
                    auto pos = *--sp;
                    auto container = *--sp;
                    if (SRCLANG_VALUE_GET_TYPE(container) ==
                            ValueType::String &&
                        SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Integer) {
                        char* buffer =
                            (char*)SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                        int index = SRCLANG_VALUE_AS_INTEGER(pos);
                        if (strlen(buffer) <= index || index < 0) {
                            error("ERROR: out-of-index trying to access '" +
                                  to_string(index) + "' from string of size '" +
                                  to_string(strlen(buffer)) + "'");
                            return false;
                        }
                        *sp++ = SRCLANG_VALUE_CHAR(buffer[index]);
                    } else if (SRCLANG_VALUE_GET_TYPE(container) ==
                                   ValueType::List &&
                               SRCLANG_VALUE_GET_TYPE(pos) ==
                                   ValueType::Integer) {
                        vector<Value> list =
                            *(vector<Value>*)SRCLANG_VALUE_AS_OBJECT(container)
                                 ->pointer;

                        int index = SRCLANG_VALUE_AS_INTEGER(pos);
                        if (list.size() <= index || index < 0) {
                            error("out-of-index trying to access '" +
                                  to_string(index) + "' from list of size '" +
                                  to_string(list.size()) + "'");
                            return false;
                        }
                        *sp++ = list[index];
                    } else {
                        if (SRCLANG_VALUE_GET_TYPE(pos) == ValueType::String) {
                            auto property =
                                (char*)(SRCLANG_VALUE_AS_OBJECT(pos)->pointer);
                            auto type_id = SRCLANG_VALUE_GET_TYPE(container);
                            if (properites[type_id].find(property) ==
                                properites[type_id].end()) {
                                error("no property '" + string(property) +
                                      "' for type '" +
                                      SRCLANG_VALUE_TYPE_ID[int(type_id)] +
                                      "'");
                                return false;
                            }
                            auto value = properites[type_id][property];
                            switch (SRCLANG_VALUE_GET_TYPE(value)) {
                                case ValueType::Function:
                                case ValueType::Builtin:
                                case ValueType::Native:
                                case ValueType::Bounded: {
                                    auto bounded_value =
                                        new BoundedValue{container, value};
                                    value = SRCLANG_VALUE_HEAP_OBJECT(
                                        ValueType::Bounded, bounded_value,
                                        ([](void* ptr) {}),
                                        ([](void* ptr) -> string {
                                            return "<bounded>";
                                        }));
                                } break;
                                default:
                                    break;
                            }
                            *sp++ = value;
                        } else {
                            error("invalid index operation");
                            return false;
                        }
                    }
                } break;

                case OpCode::SET: {
                    auto val = *--sp;
                    auto pos = *--sp;
                    auto container = *--sp;
                    if (SRCLANG_VALUE_GET_TYPE(container) ==
                            ValueType::String &&
                        SRCLANG_VALUE_GET_TYPE(pos) == ValueType::Integer) {
                        auto idx = SRCLANG_VALUE_AS_INTEGER(pos);
                        char* buf =
                            (char*)SRCLANG_VALUE_AS_OBJECT(container)->pointer;
                        int size = strlen(buf);
                        if (idx < 0 || size <= idx) {
                            error("out of bound");
                            return false;
                        }
                        if (SRCLANG_VALUE_IS_CHAR(val)) {
                            buf = (char*)realloc(buf, size);
                            if (buf == nullptr) {
                                error("out of memory");
                                return false;
                            }
                            buf[idx] = SRCLANG_VALUE_AS_CHAR(val);
                        } else if (SRCLANG_VALUE_GET_TYPE(val) ==
                                   ValueType::String) {
                            char* b =
                                (char*)SRCLANG_VALUE_AS_OBJECT(val)->pointer;
                            size_t b_size = strlen(b);
                            buf = (char*)realloc(buf, size + b_size);
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
                        auto list = reinterpret_cast<SrcLangList*>(
                            SRCLANG_VALUE_AS_OBJECT(container)->pointer);
                        list->at(SRCLANG_VALUE_AS_INTEGER(pos)) = val;
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
                } break;

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
                    char* property_id = reinterpret_cast<char*>(
                        SRCLANG_VALUE_AS_OBJECT(id)->pointer);
                    properites[SRCLANG_VALUE_AS_TYPE(ty)][property_id] = val;
                } break;

                case OpCode::RET: {
                    auto value = *--sp;
                    sp = cur()->bp - 1;
                    fp--;
                    *sp++ = value;
                    debug_info.pop_back();
                } break;

                case OpCode::JNZ: {
                    auto value = *--sp;
                    if (!SRCLANG_VALUE_AS_BOOL(value)) {
                        ip() = (cur()->fun->instructions->begin() + *ip());
                    } else {
                        *ip()++;
                    }
                } break;

                case OpCode::CONTINUE:
                case OpCode::BREAK:
                case OpCode::JMP: {
                    ip() = (cur()->fun->instructions->begin() + *ip());
                } break;

                case OpCode::HLT: {
                    return true;
                } break;

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
    for (auto const& i : args) {
        cout << srclang_value_print(i);
    }
    cout << endl;
    return SRCLANG_VALUE_INTEGER(args.size());
}

SRCLANG_BUILTIN(len) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    int length;
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::String:
            return SRCLANG_VALUE_INTEGER(
                strlen((char*)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
        case ValueType::List:
            return SRCLANG_VALUE_INTEGER(
                ((vector<Value>*)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer)
                    ->size());
        default:
            return SRCLANG_VALUE_INTEGER(sizeof(Value));
    }
}

SRCLANG_BUILTIN(append) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList*>(
                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->push_back(args[1]);
            return SRCLANG_VALUE_LIST(list);
        } break;
        case ValueType::String: {
            auto str = (char*)(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            switch (SRCLANG_VALUE_GET_TYPE(args[1])) {
                case ValueType::Char: {
                    str = (char*)realloc(str, len + 2);
                    str[len] = SRCLANG_VALUE_AS_CHAR(args[1]);
                    str[len + 1] = '\0';
                } break;
                case ValueType::String: {
                    auto str2 =
                        (char*)(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    auto len2 = strlen(str2);
                    str = (char*)realloc(str, len + len2 + 1);
                    strcat(str, str2);
                } break;
                default:
                    throw runtime_error("invalid append operation");
            }
            return SRCLANG_VALUE_STRING(str);
        } break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                ("invalid append operation on '" +
                 SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                 "'")
                    .c_str()));
    }
}

SRCLANG_BUILTIN(range) {
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

    SrcLangList* list = new SrcLangList();

    for (int i = start; i < end; i += inc) {
        list->push_back(SRCLANG_VALUE_INTEGER(i));
    }
    return SRCLANG_VALUE_LIST(list);
}

SRCLANG_BUILTIN(pop) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList*>(
                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->pop_back();
            return SRCLANG_VALUE_LIST(list);
        } break;
        case ValueType::String: {
            auto str = (char*)(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            str[len - 1] = '\0';
            return SRCLANG_VALUE_STRING(str);
        } break;
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
                           [(char*)SRCLANG_VALUE_AS_OBJECT(args[1])->pointer] =
        args[2];
    return SRCLANG_VALUE_NULL;
}

SRCLANG_BUILTIN(clone) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
        switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
            case ValueType::String: {
                return SRCLANG_VALUE_STRING(
                    strdup((char*)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
            };
            case ValueType::List: {
                SrcLangList* list = reinterpret_cast<SrcLangList*>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
                SrcLangList* new_list =
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

int main(int argc, char** argv) {
    SymbolTable symbol_table;

    bool is_interactive = true;
    optional<string> filename = nullopt;
    MemoryManager memory_manager;
    for (auto const& i : builtins) memory_manager.heap.push_back(i);

    bool debug = false;
    for (int i = 1; i < argc; i++) {
        string arg(argv[i]);
        if (arg[0] == '-') {
            arg = arg.substr(1);
            if (arg == "debug")
                debug = true;
            else {
                cerr << "ERROR: unknown flag '-" << arg << "'" << endl;
                return 1;
            }
        } else if (filename == nullopt && filesystem::exists(arg) &&
                   filesystem::path(arg).has_extension() &&
                   filesystem::path(arg).extension() == ".src") {
            filename = arg;
        } else {
            cerr << "ERROR: unknown argument " << arg << endl;
            return 1;
        }
    }

    string input;
    if (filename.has_value()) {
        ifstream reader(*filename);
        input = string((istreambuf_iterator<char>(reader)),
                       (istreambuf_iterator<char>()));
        is_interactive = false;
    }
    vector<Value> globals(20);
    {
        int i = 0;
#define X(id) symbol_table.define(#id, i++);
        SRCLANG_BUILTIN_LIST
#undef X
    }

    auto true_symbol = symbol_table.define("true");
    auto false_symbol = symbol_table.define("false");
    auto null_symbol = symbol_table.define("null");
    globals[true_symbol.index] = SRCLANG_VALUE_TRUE;
    globals[false_symbol.index] = SRCLANG_VALUE_FALSE;
    globals[null_symbol.index] = SRCLANG_VALUE_NULL;
    vector<Value> constants;

    do {
        if (is_interactive) {
            cout << ">> ";
            if (!getline(cin, input)) {
                continue;
            }
        }

        Compiler<Iterator, Byte> compiler(
            input.begin(), input.end(),
            (filename == nullopt ? "<script>" : *filename), &symbol_table,
            constants, &memory_manager);
        if (!compiler.compile()) {
            continue;
        }

        if (get<bool>(compiler.options["LOAD_LIBC"])) {
            if (dlopen(get<string>(compiler.options["LIBC"]).c_str(),
                       RTLD_GLOBAL | RTLD_NOW) == nullptr) {
                cerr << "FAILED to load libc '" << dlerror() << "'" << endl;
                return 1;
            }
        }

        auto code = move(compiler.code());
        if (debug) {
            cout << code;
            cout << "== GLOBALS ==" << endl;
            for (auto const& i : symbol_table.store) {
                if (i.second.scope == Symbol::GLOBAL) {
                    cout << "- " << i.first << " := "
                         << srclang_value_print(globals[i.second.index])
                         << endl;
                }
            }
        }

        Interpreter<Byte> interpreter{code, globals, compiler.global_debug_info,
                                      &memory_manager};
        interpreter.GC_HEAP_GROW_FACTOR =
            std::get<double>(compiler.options["GC_HEAP_GROW_FACTOR"]);
        interpreter.next_gc =
            std::get<double>(compiler.options["GC_INITIAL_TRIGGER"]);
        interpreter.debug = debug;
        if (!interpreter.run()) {
            continue;
        }

    } while (is_interactive);
}
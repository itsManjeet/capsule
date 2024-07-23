#ifndef SRCLANG_INTERPRETER_H
#define SRCLANG_INTERPRETER_H

#include "ByteCode.h"
#include "Compiler.h"
#include "Function.h"
#include "MemoryManager.h"
#include "SymbolTable.h"
#include "Value.h"
#include <filesystem>
#include <sstream>
#include <variant>

namespace SrcLang {

class Interpreter {
public:
    using OptionType = std::variant<std::wstring, int, float, bool>;

    class Options : public std::map<std::wstring, OptionType> {
    public:
        explicit Options(std::map<std::wstring, OptionType> const& options)
                : std::map<std::wstring, OptionType>(options) {}
    };

private:
    struct Frame {
        typename std::vector<Byte>::iterator ip;
        Value closure;
        std::vector<Value>::iterator bp;
        std::vector<Value> defers;
    };

    struct Context {
        std::vector<Value> stack;
        std::vector<Value>::iterator sp;
        std::vector<Frame> frames;
        std::vector<Frame>::iterator fp;

        Context()
                : stack(1024), sp(stack.begin()), frames(2048),
                  fp(frames.begin()) {}
    };

    std::vector<Context> contexts;
    std::vector<Context>::iterator cp;
    Options options;
    friend Compiler;

    int nextGc = 50;
    float gcHeapGrowFactor = 1.5;

    // TODO run GC based on bytes not on object count
    int limitNextGc = 200;

    std::vector<Value> globals;
    std::vector<Value> constants;
    MemoryManager memoryManager;
    SymbolTable symbolTable;

    std::map<std::wstring, std::pair<void*, Value>> handlers;

    std::wstringstream errStream;

    bool debug{}, break_{};

    void error(std::wstring const& msg);

    void error(std::string const& msg) { return error(s2ws(msg)); }

    bool isEqual(Value a, Value b);

    bool unary(Value a, OpCode op);

    bool binary(Value lhs, Value rhs, OpCode op);

    static bool isFalsy(Value val);

    void debug_stack(Context *ctxt);

    bool callClosure(Value callee, uint8_t count);

    bool callNative(Value callee, uint8_t count);

    bool callBuiltin(Value callee, uint8_t count);

    bool callMap(Value callee, uint8_t count);

    bool callTypecastNum(uint8_t count);

    bool callTypecastString(uint8_t count);

    bool callTypecastError(uint8_t count);

    bool callTypecastBool(uint8_t count);

    bool callTypecastType(uint8_t count);

    bool callTypecast(Value callee, uint8_t count);

    bool callBounded(Value callee, uint8_t count);

    bool call(uint8_t count);

    bool run();

    static Interpreter* m_globalActive;

public:
    Interpreter();

    template <typename T> void setOption(const std::wstring& id, T t) {
        options[id] = t;
    }

    template <typename T> T getOption(const std::wstring& id) {
        return std::get<T>(options[id]);
    }

    void appendOption(const std::wstring& id, std::wstring value,
            const std::wstring& sep = L":") {
        options[id] = std::get<std::wstring>(options[id]) + sep + value;
    }

    void prependOption(const std::wstring& id, std::wstring value,
            const std::wstring& sep = L":") {
        options[id] = value + sep + std::get<std::wstring>(options[id]);
    }

    ~Interpreter();

    void graceFullExit();

    Value addObject(Value val);

    void gc();

    void push_context();

    void pop_context();

    Value call(Value callee, const std::vector<Value>& args);

    Value run(Value callee);

    Value run(const std::wstring& source, const std::wstring& filename);

    std::wstring search(const std::wstring& id);

    Value loadModule(std::wstring modulePath);

    std::wstring getError() {
        std::wstring e = errStream.str();
        errStream.clear();
        return e;
    }

    static Interpreter* globalActive() { return m_globalActive; }
};

} // namespace SrcLang

#endif // SRCLANG_INTERPRETER_H

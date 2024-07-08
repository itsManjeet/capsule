#ifndef SRCLANG_INTERPRETER_H
#define SRCLANG_INTERPRETER_H

#include <sstream>
#include <filesystem>

#include "MemoryManager.h"
#include "ByteCode.h"
#include "Function.h"
#include "Value.h"
#include "SymbolTable.h"

namespace SrcLang {

    class Interpreter {
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

            Context() : stack(1024), sp(stack.begin()),
                        frames(2048), fp(frames.begin()) {

            }
        };

        std::vector<Context> contexts;
        std::vector<Context>::iterator cp;

        int nextGc = 50;
        float gcHeapGrowFactor = 1.5;

        // TODO run GC based on bytes not on object count
        int limitNextGc = 200;

        std::vector<Value> globals;
        std::vector<Value> constants;
        MemoryManager memoryManager;
        SymbolTable symbolTable;

        std::map<std::string, std::pair<void *, Value>> handlers;

        std::stringstream errStream;

        std::vector<std::shared_ptr<DebugInfo>> debugInfo;
        bool debug{}, break_{};

        void error(std::string const &msg);

        bool isEqual(Value a, Value b);

        bool unary(Value a, OpCode op);

        bool binary(Value lhs, Value rhs, OpCode op);

        static bool isFalsy(Value val);

        void printStack();

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

        Value loadModule(const std::filesystem::path &modulePath);

        Value run(ByteCode &bytecode, const std::shared_ptr<DebugInfo> &debugInfo);

    public:
        Interpreter();

        ~Interpreter();

        void graceFullExit();

        void addObject(Value val);

        void gc();

        void push_context();

        void pop_context();

        Value call(Value callee, const std::vector<Value> &args);

        Value run(const std::string &source, const std::string &filename);

        std::string getError() {
            std::string e = errStream.str();
            errStream.clear();
            return e;
        }
    };

}  // SrcLang

#endif  // SRCLANG_INTERPRETER_H

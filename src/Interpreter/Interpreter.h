#ifndef SRCLANG_INTERPRETER_H
#define SRCLANG_INTERPRETER_H

#include <sstream>

#include "../Builtins/Builtin.h"
#include "../ByteCode/ByteCode.h"
#include "../Value/Function.h"
#include "../Language/Options.h"
#include "../Value/Value.h"

namespace srclang {

    struct Language;

    struct Interpreter {
        struct Frame {
            typename std::vector<Byte>::iterator ip;
            Closure *closure;
            std::vector<Value>::iterator bp;
            std::vector<Value> defers;
        };

        int nextGc = 50;
        float gcHeapGrowFactor = 1.5;

        // TODO run GC based on bytes not on object count
        int limitNextGc = 200;

        std::vector<Value> stack;
        std::vector<Value>::iterator sp;
        std::vector<Frame> frames;

        Language *language;

        std::stringstream errStream;

        typename std::vector<Frame>::iterator fp;
        std::vector<std::shared_ptr<DebugInfo>> debugInfo;
        bool debug, break_;

        void error(std::string const &msg);

        void graceFullExit();

        Interpreter(ByteCode &code, const std::shared_ptr<DebugInfo> &debugInfo, Language *language);

        ~Interpreter();

        void addObject(Value val);

        void gc();

        bool isEqual(Value a, Value b);

        bool unary(Value a, OpCode op);

        bool binary(Value lhs, Value rhs, OpCode op);

        static bool isFalsy(Value val);

        void printStack();

        bool callClosure(Value callee, uint8_t count);

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

        std::string getError() {
            std::string e = errStream.str();
            errStream.clear();
            return e;
        }
    };

}  // srclang

#endif  // SRCLANG_INTERPRETER_H

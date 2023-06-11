#ifndef SRCLANG_BUILTIN_HXX
#define SRCLANG_BUILTIN_HXX

#include "Value.hxx"
#include "MemoryManager.hxx"

namespace srclang {
    struct Interpreter;

    typedef Value (*Builtin)(std::vector<Value> &, Interpreter *);

#define SRCLANG_BUILTIN(id)                       \
    Value builtin_##id(std::vector<Value> const& args, \
                       Interpreter* interpreter)
#define SRCLANG_BUILTIN_LIST \
    X(println)               \
    X(gc)                    \
    X(len)                   \
    X(append)                \
    X(range)                 \
    X(clone)                 \
    X(eval)                  \
    X(pop)                   \
    X(lower)                 \
    X(upper)                 \
    X(search)

    struct Interpreter;
#define X(id) SRCLANG_BUILTIN(id);

    SRCLANG_BUILTIN_LIST

#undef X

    enum Builtins {
#define X(id) BUILTIN_##id,
        SRCLANG_BUILTIN_LIST
#undef X
    };

    extern std::vector<Value> builtins;

} // srclang

#endif //SRCLANG_BUILTIN_HXX

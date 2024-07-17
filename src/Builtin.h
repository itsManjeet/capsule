#ifndef SRCLANG_BUILTIN_H
#define SRCLANG_BUILTIN_H

#include "MemoryManager.h"
#include "Value.h"

namespace SrcLang {
class Interpreter;

typedef Value (*Builtin)(std::vector<Value>&, Interpreter*);

#define SRCLANG_BUILTIN(id)                                                    \
    Value builtin_##id(std::vector<Value> const& args, Interpreter* interpreter)
#define SRCLANG_BUILTIN_LIST                                                   \
    X(println)                                                                 \
    X(print)                                                                   \
    X(gc)                                                                      \
    X(len)                                                                     \
    X(append)                                                                  \
    X(range)                                                                   \
    X(clone)                                                                   \
    X(eval)                                                                    \
    X(pop)                                                                     \
    X(alloc)                                                                   \
    X(free)                                                                    \
    X(bound)

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
} // namespace SrcLang

#endif // SRCLANG_BUILTIN_H

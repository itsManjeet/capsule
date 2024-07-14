#include <Interpreter.h>
using namespace SrcLang;

#include <math.h>

#define DEFINE_FUNC_1(id, fun)                                              \
    SRCLANG_MODULE_FUNC(id) {                                               \
        SRCLANG_CHECK_ARGS_EXACT(1);                                        \
        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);                      \
        return SRCLANG_VALUE_NUMBER(fun(SRCLANG_VALUE_AS_NUMBER(args[0]))); \
    }

DEFINE_FUNC_1(Abs, abs);
DEFINE_FUNC_1(Round, round);

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE(PI, SRCLANG_VALUE_NUMBER(M_PI));
    SRCLANG_MODULE_DEFINE_FUNC(Abs);
    SRCLANG_MODULE_DEFINE_FUNC(Round);
}
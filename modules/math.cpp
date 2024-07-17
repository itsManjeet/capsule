#include <Interpreter.h>
using namespace SrcLang;

#include <cmath>
#include <float.h>

#define DEFINE_FUNC_1(id, fun)                                                 \
    SRCLANG_MODULE_FUNC(id) {                                                  \
        SRCLANG_CHECK_ARGS_EXACT(1);                                           \
        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);                         \
        return SRCLANG_VALUE_NUMBER(fun(SRCLANG_VALUE_AS_NUMBER(args[0])));    \
    }

#define DEFINE_FUNC_2(id, fun)                                                 \
    SRCLANG_MODULE_FUNC(id) {                                                  \
        SRCLANG_CHECK_ARGS_EXACT(2);                                           \
        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);                         \
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);                         \
        return SRCLANG_VALUE_NUMBER(fun(SRCLANG_VALUE_AS_NUMBER(args[0]),      \
                SRCLANG_VALUE_AS_NUMBER(args[1])));                            \
    }

#define MATH_FUNC_1                                                            \
    X(Abs, abs)                                                                \
    X(Acos, acos)                                                              \
    X(Acosh, acosh)                                                            \
    X(Asin, asin)                                                              \
    X(Asinh, asinh)                                                            \
    X(Atan, atan)                                                              \
    X(Atanh, atanh)                                                            \
    X(Cbrt, cbrt)                                                              \
    X(Ceil, ceil)                                                              \
    X(Cos, cos)                                                                \
    X(Cosh, cosh)                                                              \
    X(Erf, erf)                                                                \
    X(Erfc, erfc)                                                              \
    X(Exp, exp)                                                                \
    X(Exp2, exp2)                                                              \
    X(Expm1, expm1)                                                            \
    X(Floor, floor)                                                            \
    X(Gamma, gamma)                                                            \
    X(Ilogb, ilogb)                                                            \
    X(J0, j0)                                                                  \
    X(J1, j1)                                                                  \
    X(Log, log)                                                                \
    X(Log10, log10)                                                            \
    X(Log1p, log1p)                                                            \
    X(Log2, log2)                                                              \
    X(Logb, logb)                                                              \
    X(Round, round)                                                            \
    X(RoundToEven, roundeven)                                                  \
    X(Sin, sin)                                                                \
    X(Sinh, sinh)                                                              \
    X(Sqrt, sqrt)                                                              \
    X(Tan, tan)                                                                \
    X(Tanh, tanh)                                                              \
    X(Trunc, trunc)                                                            \
    X(Y0, y0)                                                                  \
    X(Y1, y1)

#define MATH_FUNC_2                                                            \
    X(Atan2, atan2)                                                            \
    X(Copysign, copysign)                                                      \
    X(Hypot, hypot)                                                            \
    X(Pow, pow)                                                                \
    X(Remainder, remainder)                                                    \
    X(Yn, yn)

#define X(id, fun) DEFINE_FUNC_1(id, fun)
MATH_FUNC_1
#undef X

#define X(id, fun) DEFINE_FUNC_2(id, fun)
MATH_FUNC_2
#undef X

SRCLANG_MODULE_FUNC(Min) {
    double num = DBL_MAX;
    for (int i = 0; i < args.size(); i++) {
        SRCLANG_CHECK_ARGS_TYPE(i, ValueType::Number);
        if (SRCLANG_VALUE_AS_NUMBER(args[i]) < num) {
            num = SRCLANG_VALUE_AS_NUMBER(args[i]);
        }
    }
    return SRCLANG_VALUE_AS_NUMBER(num);
}

SRCLANG_MODULE_FUNC(Max) {
    double num = DBL_MIN;
    for (int i = 0; i < args.size(); i++) {
        SRCLANG_CHECK_ARGS_TYPE(i, ValueType::Number);
        if (SRCLANG_VALUE_AS_NUMBER(args[i]) > num) {
            num = SRCLANG_VALUE_AS_NUMBER(args[i]);
        }
    }
    return SRCLANG_VALUE_AS_NUMBER(num);
}

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE(PI, SRCLANG_VALUE_NUMBER(M_PI));
    SRCLANG_MODULE_DEFINE(E, SRCLANG_VALUE_NUMBER(M_E));

#define X(id, fun) SRCLANG_MODULE_DEFINE_FUNC(id);
    MATH_FUNC_1
    MATH_FUNC_2
#undef X

    SRCLANG_MODULE_DEFINE_FUNC(Max);
    SRCLANG_MODULE_DEFINE_FUNC(Min);
}
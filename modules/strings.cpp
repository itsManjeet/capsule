#include <Interpreter.h>
using namespace SrcLang;

SRCLANG_MODULE_FUNC(Compare) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
    return SRCLANG_VALUE_BOOL(wcscmp(SRCLANG_VALUE_AS_STRING(args[0]),
                                      SRCLANG_VALUE_AS_STRING(args[1])) == 0);
}

SRCLANG_MODULE_FUNC(Contains) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
    return SRCLANG_VALUE_BOOL(wcscmp(SRCLANG_VALUE_AS_STRING(args[0]),
                                      SRCLANG_VALUE_AS_STRING(args[1])) == 0);
}

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE_FUNC(Compare);
    SRCLANG_MODULE_DEFINE_FUNC(Contains)
    ;
}
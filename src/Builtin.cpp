#include "Builtin.h"

#include "Interpreter.h"

using namespace SrcLang;

#ifdef _WIN32
#    include <windows.h>
#    define popen _popen
#    define pclose _pclose
#    define WEXITSTATUS
#else
#    include <sys/ioctl.h>
#    include <sys/stat.h>
#endif

std::vector<Value> SrcLang::builtins = {
#define X(id) SRCLANG_VALUE_BUILTIN(id),
        SRCLANG_BUILTIN_LIST
#undef X
};

#undef SRCLANG_BUILTIN

#define SRCLANG_BUILTIN(id)                                                    \
    Value SrcLang::builtin_##id(                                               \
            std::vector<Value> const& args, Interpreter* interpreter)

SRCLANG_BUILTIN(gc) {
    SRCLANG_CHECK_ARGS_NONE;
    interpreter->gc();

    return SRCLANG_VALUE_TRUE;
}

SRCLANG_BUILTIN(print) {
    for (auto const& i : args) { std::wcout << SRCLANG_VALUE_GET_STRING(i); }
    std::wcout << std::flush;
    return SRCLANG_VALUE_NUMBER(args.size());
}

SRCLANG_BUILTIN(println) {
    for (auto const& i : args) { std::wcout << SRCLANG_VALUE_GET_STRING(i); }
    std::wcout << std::endl;
    return SRCLANG_VALUE_NUMBER(args.size());
}

SRCLANG_BUILTIN(len) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
    case ValueType::String:
        return SRCLANG_VALUE_NUMBER(wchlen(SRCLANG_VALUE_AS_STRING(args[0])));
    case ValueType::List:
        return SRCLANG_VALUE_NUMBER(SRCLANG_VALUE_AS_LIST(args[0])->size());
    case ValueType::Pointer:
        return SRCLANG_VALUE_NUMBER(SRCLANG_VALUE_AS_OBJECT(args[0])->size);

    default: return SRCLANG_VALUE_NUMBER(sizeof(Value));
    }
}

SRCLANG_BUILTIN(append) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
    case ValueType::List: {
        auto list = SRCLANG_VALUE_AS_LIST(args[0]);
        list->push_back(args[1]);
        return SRCLANG_VALUE_LIST(list);
    }
    case ValueType::String: {
        auto str = SRCLANG_VALUE_AS_STRING(args[0]);
        auto const len = wchlen(str);
        switch (SRCLANG_VALUE_GET_TYPE(args[1])) {
        case ValueType::String: {
            auto str2 = SRCLANG_VALUE_AS_STRING(args[1]);
            auto const len2 = wchlen(str2);
            wchar_t* new_str = str;
            str = static_cast<wchar_t*>(realloc(new_str, len + len2 + 1));
            if (str == nullptr) {
                free(new_str);
                throw std::runtime_error(
                        "realloc() failed, SrcLang::append(), " +
                        std::string(strerror(errno)));
            }
            memccpy(new_str, str, static_cast<int>(len), sizeof(wchar_t));
            memccpy(new_str + len, str2, static_cast<int>(len2),
                    sizeof(wchar_t));
            return SRCLANG_VALUE_STRING(new_str);
        }
        default: throw std::runtime_error("invalid append operation");
        }
    }
    default:
        return SRCLANG_VALUE_ERROR(strdup((
                "invalid append operation on '" +
                SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                "'")
                                                  .c_str()));
    }
}

SRCLANG_BUILTIN(range) {
    SRCLANG_CHECK_ARGS_RANGE(1, 3);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    int start = 0;
    int inc = 1;
    int end = SRCLANG_VALUE_AS_NUMBER(args[0]);

    if (args.size() == 2) {
        start = end;
        end = SRCLANG_VALUE_AS_NUMBER(args[1]);
    } else if (args.size() == 3) {
        start = end;
        end = SRCLANG_VALUE_AS_NUMBER(args[1]);
        inc = SRCLANG_VALUE_AS_NUMBER(args[2]);
    }

    if (inc < 0) {
        start = end;
        end = start;
        inc = -inc;
    }

    auto const list = new SrcLangList();

    for (int i = start; i < end; i += inc) {
        list->push_back(SRCLANG_VALUE_NUMBER(i));
    }
    return SRCLANG_VALUE_LIST(list);
}

SRCLANG_BUILTIN(pop) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
    case ValueType::List: {
        auto const list = SRCLANG_VALUE_AS_LIST(args[0]);
        list->pop_back();
        return SRCLANG_VALUE_LIST(list);
    }
    case ValueType::String: {
        auto const str = SRCLANG_VALUE_AS_STRING(args[0]);
        auto const len = wchlen(str);
        str[len - 1] = '\0';
        return SRCLANG_VALUE_STRING(str);
    }
    default:
        return SRCLANG_VALUE_ERROR(
                strdup(("invalid pop operation on '" +
                        SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                                SRCLANG_VALUE_GET_TYPE(args[0]))] +
                        "'")
                                .c_str()));
    }
}

SRCLANG_BUILTIN(clone) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
        switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::String:
        case ValueType::Error: {
            return SRCLANG_VALUE_STRING(
                    wchdup(SRCLANG_VALUE_AS_STRING(args[0])));
        }
        case ValueType::List: {
            auto const list = SRCLANG_VALUE_AS_LIST(args[0]);
            return SRCLANG_VALUE_LIST(
                    new SrcLangList(list->begin(), list->end()));
        }
        case ValueType::Pointer: {
            auto const object = SRCLANG_VALUE_AS_OBJECT(args[0]);
            if (object->is_ref) { return args[0]; }
            auto const buffer = malloc(object->size);
            if (buffer == nullptr) {
                throw std::runtime_error(
                        "can't malloc() for SrcLang::clone() " +
                        std::string(strerror(errno)));
            }
            memcpy(buffer, object->pointer, object->size);
            return SRCLANG_VALUE_SET_SIZE(
                    SRCLANG_VALUE_POINTER(buffer), object->size);
        }
        default:
            if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
                return SRCLANG_VALUE_ERROR(
                        wcsdup(s2ws("invalid clone operation on '" +
                                    SRCLANG_VALUE_TYPE_ID[static_cast<int>(
                                            SRCLANG_VALUE_GET_TYPE(args[0]))] +
                                    "'")
                                        .c_str()));
            }
        }
    }
    return args[0];
}

SRCLANG_BUILTIN(eval) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::List);
    auto result = interpreter->run(SRCLANG_VALUE_AS_STRING(args[0]),
            L"<inline>", *SRCLANG_VALUE_AS_LIST(args[1]));
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        return builtin_clone({result}, interpreter);
    }
    return result;
}

SRCLANG_BUILTIN(call) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Closure);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::List);
    auto result = interpreter->call(args[0], *SRCLANG_VALUE_AS_LIST(args[1]));
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        return builtin_clone({result}, interpreter);
    }
    return result;
}

SRCLANG_BUILTIN(alloc) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Type);
    long const size = SRCLANG_VALUE_AS_NUMBER(args[0]);
    switch (SRCLANG_VALUE_AS_TYPE(args[1])) {
    case ValueType::Pointer: {
        return SRCLANG_VALUE_SET_SIZE(
                SRCLANG_VALUE_POINTER(new unsigned char[size]), size);
    }

    case ValueType::String: {
        auto* ptr = new wchar_t[size + 1];
        for (int i = 0; i < size; i++) ptr[i] = L' ';
        ptr[size] = L'\0';
        return SRCLANG_VALUE_STRING(ptr);
    }

    case ValueType::List: return SRCLANG_VALUE_LIST(new SrcLangList(size));
    default: throw std::runtime_error("invalid type");
    }
}

SRCLANG_BUILTIN(bound) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    return SRCLANG_VALUE_BOUND(args[0], args[1]);
}

SRCLANG_BUILTIN(free) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    auto const object = SRCLANG_VALUE_AS_OBJECT(args[0]);
    if (object->cleanup == nullptr) { object->cleanup = free; }
    object->cleanup(object->pointer);
    object->pointer = nullptr;
    return SRCLANG_VALUE_TRUE;
}

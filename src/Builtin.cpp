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
#    include <termios.h>
#    include <unistd.h>
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
    SRCLANG_CHECK_ARGS_EXACT(0);
    interpreter->gc();

    return SRCLANG_VALUE_TRUE;
}

SRCLANG_BUILTIN(print) {
    for (auto const& i : args) { std::wcout << SRCLANG_VALUE_GET_STRING(i); }
    std::wcout << std::flush;
    return SRCLANG_VALUE_NUMBER(args.size());
}

SRCLANG_BUILTIN(println) {
    for (auto const& i : args) {
        std::wcout <<  SRCLANG_VALUE_GET_STRING(i);
    }
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
    } break;
    case ValueType::String: {
        auto str = SRCLANG_VALUE_AS_STRING(args[0]);
        auto const len = wchlen(str);
        switch (SRCLANG_VALUE_GET_TYPE(args[1])) {
        case ValueType::String: {
            auto str2 = SRCLANG_VALUE_AS_STRING(args[1]);
            auto const len2 = wchlen(str2);
            str = static_cast<wchar_t*>(realloc(str, len + len2 + 1));
            if (str == nullptr) {
                throw std::runtime_error(
                        "realloc() failed, SrcLang::append(), " +
                        std::string(strerror(errno)));
            }
            memccpy(str + len, str2, len2, sizeof(wchar_t));
        } break;
        default: throw std::runtime_error("invalid append operation");
        }
        return SRCLANG_VALUE_STRING(str);
    } break;
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
        auto t = start;
        start = end;
        end = start;
        inc = -inc;
    }

    auto list = new SrcLangList();

    for (int i = start; i < end; i += inc) {
        list->push_back(SRCLANG_VALUE_NUMBER(i));
    }
    return SRCLANG_VALUE_LIST(list);
}

SRCLANG_BUILTIN(pop) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
    case ValueType::List: {
        auto list = SRCLANG_VALUE_AS_LIST(args[0]);
        list->pop_back();

        return SRCLANG_VALUE_LIST(list);
    } break;
    case ValueType::String: {
        auto const str = SRCLANG_VALUE_AS_STRING(args[0]);
        auto const len = wchlen(str);
        str[len - 1] = '\0';
        return SRCLANG_VALUE_STRING(str);
    } break;
    default:
        return SRCLANG_VALUE_ERROR(strdup((
                "invalid pop operation on '" +
                SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
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
        };
        case ValueType::List: {
            auto list = SRCLANG_VALUE_AS_LIST(args[0]);
            auto new_list = new SrcLangList(list->begin(), list->end());
            return SRCLANG_VALUE_LIST(new_list);
        };
        case ValueType::Pointer: {
            auto object = SRCLANG_VALUE_AS_OBJECT(args[0]);
            if (object->is_ref) { return args[0]; }
            auto buffer = malloc(object->size);
            if (buffer == nullptr) {
                throw std::runtime_error(
                        "can't malloc() for SrcLang::clone() " +
                        std::string(strerror(errno)));
            }
            memcpy(buffer, object->pointer, object->size);
            auto value = SRCLANG_VALUE_POINTER(buffer);
            SRCLANG_VALUE_SET_SIZE(value, object->size);
            return value;
        } break;
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

SRCLANG_BUILTIN(eval) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto* buffer = SRCLANG_VALUE_AS_STRING(args[0]);
    auto result = interpreter->run((wchar_t*)buffer, L"<inline>");
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        return builtin_clone({result}, interpreter);
    }
    return result;
}

SRCLANG_BUILTIN(alloc) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Type);
    long size = SRCLANG_VALUE_AS_NUMBER(args[0]);
    switch (SRCLANG_VALUE_AS_TYPE(args[1])) {
    case ValueType::Pointer: {
        void* ptr = calloc(size * sizeof(unsigned char), 1);
        if (ptr == nullptr) {
            std::stringstream ss;
            ss << "failed to allocate '" << size << "' size, "
               << strerror(errno);
            return SRCLANG_VALUE_ERROR(strdup(ss.str().c_str()));
        }
        auto value = SRCLANG_VALUE_POINTER(ptr);
        SRCLANG_VALUE_SET_SIZE(value, size);
        return value;
    } break;

    case ValueType::String: {
        auto* ptr = static_cast<wchar_t*>(malloc((size + 1) * sizeof(wchar_t)));
        if (ptr == nullptr) {
            std::wstringstream ss;
            ss << L"failed to allocate '" << size << L"' size, "
               << s2ws(strerror(errno));
            return SRCLANG_VALUE_ERROR(wchdup(ss.str().c_str()));
        }
        for (int i = 0; i < size; i++) ptr[i] = L' ';
        ptr[size] = L'\0';
        auto value = SRCLANG_VALUE_STRING(ptr);
        return value;
    } break;

    case ValueType::List: {
        auto list = new SrcLangList(size);
        return SRCLANG_VALUE_LIST(list);
    } break;
    }
    return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(L"invalid type"));
}

SRCLANG_BUILTIN(bound) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    return SRCLANG_VALUE_BOUND(args[0], args[1]);
}

SRCLANG_BUILTIN(free) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    auto object = SRCLANG_VALUE_AS_OBJECT(args[0]);
    if (object->cleanup == nullptr) { object->cleanup = free; }
    object->cleanup(object->pointer);
    object->pointer = nullptr;
    return SRCLANG_VALUE_TRUE;
}

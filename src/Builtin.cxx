#include "Builtin.hxx"
#include "Interpreter.hxx"
#include "Language.hxx"

using namespace srclang;

std::vector<Value> srclang::builtins = {
#define X(id) SRCLANG_VALUE_BUILTIN(id),
        SRCLANG_BUILTIN_LIST
#undef X
};

#undef SRCLANG_BUILTIN

#define SRCLANG_BUILTIN(id)                       \
    Value srclang::builtin_##id(std::vector<Value> const& args, \
                       Interpreter* interpreter)

SRCLANG_BUILTIN(gc) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    interpreter->gc();

    return SRCLANG_VALUE_TRUE;
}

SRCLANG_BUILTIN(call) {
    SRCLANG_CHECK_ARGS_ATLEAST(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Closure);
    auto callee = args[0];
    SrcLangList callee_args;
    if (args.size() > 1) {
        callee_args = std::vector<Value>(args.begin() + 1, args.end());
    }
    return interpreter->language->call(callee, callee_args);
}

SRCLANG_BUILTIN(print) {
    for (auto const &i: args) {
        std::cout << SRCLANG_VALUE_GET_STRING(i);
    }
    return SRCLANG_VALUE_INTEGER(args.size());
}


SRCLANG_BUILTIN(println) {
    for (auto const &i: args) {
        std::cout << SRCLANG_VALUE_GET_STRING(i);
    }
    std::cout << std::endl;
    return SRCLANG_VALUE_INTEGER(args.size());
}

SRCLANG_BUILTIN(len) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    int length;
    switch (
            SRCLANG_VALUE_GET_TYPE(args[0])
            ) {
        case ValueType::String:
            return SRCLANG_VALUE_INTEGER(
                    strlen((char *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
        case ValueType::List:
            return SRCLANG_VALUE_INTEGER(
                    ((std::vector<Value> *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer)
                            ->size());
        default:
            return SRCLANG_VALUE_INTEGER(sizeof(Value));
    }
}

SRCLANG_BUILTIN(append) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    switch (
            SRCLANG_VALUE_GET_TYPE(args[0])
            ) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->
                    push_back(args[1]);
            return SRCLANG_VALUE_LIST(list);
        }
            break;
        case ValueType::String: {
            auto str = (char *) (SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            switch (
                    SRCLANG_VALUE_GET_TYPE(args[1])
                    ) {
                case ValueType::Char: {
                    str = (char *) realloc(str, len + 2);
                    str[len] = SRCLANG_VALUE_AS_CHAR(args[1]);
                    str[len + 1] = '\0';
                }
                    break;
                case ValueType::String: {
                    auto str2 =
                            (char *) (SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    auto len2 = strlen(str2);
                    str = (char *) realloc(str, len + len2 + 1);
                    strcat(str, str2
                    );
                }
                    break;
                default:
                    throw std::runtime_error("invalid append operation");
            }
            return SRCLANG_VALUE_STRING(str);
        }
            break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                    ("invalid append operation on '" +
                     SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                     "'")
                            .c_str()));
    }
}

SRCLANG_BUILTIN(range) {
    if (args.size() == 1 &&
        SRCLANG_VALUE_GET_TYPE(args[0]) == ValueType::Map) {
        auto map = reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
        auto keys = new SrcLangList();
        for (auto i = map->begin(); i != map->end(); i++) {
            auto v = SRCLANG_VALUE_STRING(strdup(i->first.c_str()));
            keys->push_back(v);
        }
        auto list = SRCLANG_VALUE_LIST(keys);
        return list;
    }

    SRCLANG_CHECK_ARGS_RANGE(1, 3);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Integer);
    int start = 0;
    int inc = 1;
    int end = SRCLANG_VALUE_AS_INTEGER(args[0]);

    if (args.size() == 2) {
        start = end;
        end = SRCLANG_VALUE_AS_INTEGER(args[1]);
    } else if (args.size() == 3) {
        start = end;
        end = SRCLANG_VALUE_AS_INTEGER(args[1]);
        inc = SRCLANG_VALUE_AS_INTEGER(args[2]);
    }

    if (inc < 0) {
        auto t = start;
        start = end;
        end = start;
        inc = -inc;
    }

    auto list = new SrcLangList();

    for (int i = start; i < end; i += inc) {
        list->push_back(SRCLANG_VALUE_INTEGER(i));
    }
    return SRCLANG_VALUE_LIST(list);
}

SRCLANG_BUILTIN(pop) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->pop_back();

            return SRCLANG_VALUE_LIST(list);
        }
            break;
        case ValueType::String: {
            auto str = (char *) (SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            str[len - 1] = '\0';
            return SRCLANG_VALUE_STRING(str);
        }
            break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                    ("invalid pop operation on '" +
                     SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_GET_TYPE(args[0]))] +
                     "'")
                            .c_str()));
    }
}


SRCLANG_BUILTIN(clone) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    if (SRCLANG_VALUE_IS_OBJECT(args[0])) {
        switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
            case ValueType::String: {
                return SRCLANG_VALUE_STRING(
                        strdup((char *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
            };
            case ValueType::List: {
                auto list = reinterpret_cast<SrcLangList *>(
                        SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
                auto new_list =
                        new SrcLangList(list->begin(), list->end());
                return SRCLANG_VALUE_LIST(new_list);
            };
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

SRCLANG_BUILTIN(lower) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = (char) ::tolower(buffer[i]);
    }
    return SRCLANG_VALUE_STRING(newBuffer);
}

SRCLANG_BUILTIN(upper) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = (char) ::toupper(buffer[i]);
    }
    return SRCLANG_VALUE_STRING(newBuffer);
}

SRCLANG_BUILTIN(search) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    auto containerType = SRCLANG_VALUE_GET_TYPE(args[0]);
    auto valueType = SRCLANG_VALUE_GET_TYPE(args[1]);

    switch (containerType) {
        case ValueType::String: {
            char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
            switch (valueType) {
                case ValueType::Char: {
                    char val = SRCLANG_VALUE_AS_CHAR(args[1]);
                    for (
                            int i = 0;
                            i < strlen(buffer);
                            i++) {
                        if (buffer[i] == val) {
                            return SRCLANG_VALUE_INTEGER(i);
                        }
                    }
                    return SRCLANG_VALUE_INTEGER(-1);
                }
                    break;

                case ValueType::String: {
                    char *val = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    char *result = strstr(buffer, val);
                    if (result == nullptr) {
                        return SRCLANG_VALUE_INTEGER(-1);
                    }
                    return SRCLANG_VALUE_INTEGER(strlen(buffer) - (result - buffer));
                }
                    break;

                default:
                    throw std::runtime_error(
                            "can't search value '" +
                            SRCLANG_VALUE_DEBUG(args[1]) + "' in string container");

            }

        }
            break;

        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            for (int i = 0; i < list->size(); i++) {
                if (interpreter->isEqual(args[1], list->at(i))) {
                    return SRCLANG_VALUE_INTEGER(i);
                }
            }
            return SRCLANG_VALUE_INTEGER(-1);
        }
            break;

        case ValueType::Map: {
            SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
            auto map = reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            return SRCLANG_VALUE_BOOL(map->find((char *) SRCLANG_VALUE_AS_OBJECT(args[1])->pointer) != map->end());
        }
    }

    return SRCLANG_VALUE_INTEGER(-1);
}


SRCLANG_BUILTIN(eval) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    const char *buffer = (const char *) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer;
    return interpreter->language->execute(buffer, "<inline>");
}

SRCLANG_BUILTIN(alloc) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Integer);
    void *ptr = malloc(SRCLANG_VALUE_AS_INTEGER(SRCLANG_VALUE_AS_INTEGER(args[0])));
    return SRCLANG_VALUE_POINTER(ptr);
}

SRCLANG_BUILTIN(free) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    free(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    return SRCLANG_VALUE_TRUE;
}
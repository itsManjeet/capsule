#include "Builtin.hxx"

#include "Interpreter.hxx"
#include "Language.hxx"
#include <ffi.h>
#include <dlfcn.h>
#include <libtcc.h>

using namespace srclang;

std::vector<Value> srclang::builtins = {
#define X(id) SRCLANG_VALUE_BUILTIN(id),
    SRCLANG_BUILTIN_LIST
#undef X
};

#undef SRCLANG_BUILTIN

#define SRCLANG_BUILTIN(id)                                     \
    Value srclang::builtin_##id(std::vector<Value> const &args, \
                                Interpreter *interpreter)

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
    for (auto const &i : args) {
        std::cout << SRCLANG_VALUE_GET_STRING(i);
    }
    return SRCLANG_VALUE_NUMBER(args.size());
}

SRCLANG_BUILTIN(println) {
    std::string sep;
    for (auto const &i : args) {
        std::cout << sep << SRCLANG_VALUE_GET_STRING(i);
        sep = " ";
    }
    std::cout << std::endl;
    return SRCLANG_VALUE_NUMBER(args.size());
}

SRCLANG_BUILTIN(len) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    switch (
        SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::String:
            return SRCLANG_VALUE_NUMBER(
                strlen((char *)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
        case ValueType::List:
            return SRCLANG_VALUE_NUMBER(
                ((std::vector<Value> *)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer)
                    ->size());
        case ValueType::Pointer:
            return SRCLANG_VALUE_NUMBER(SRCLANG_VALUE_AS_OBJECT(args[0])->size);

        default:
            return SRCLANG_VALUE_NUMBER(sizeof(Value));
    }
}

SRCLANG_BUILTIN(append) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    switch (
        SRCLANG_VALUE_GET_TYPE(args[0])) {
        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(
                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->push_back(args[1]);
            return SRCLANG_VALUE_LIST(list);
        } break;
        case ValueType::String: {
            auto str = (char *)(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            switch (SRCLANG_VALUE_GET_TYPE(args[1])) {
                break;
                case ValueType::String: {
                    auto str2 =
                        (char *)(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    auto len2 = strlen(str2);
                    str = (char *)realloc(str, len + len2 + 1);
                    if (str == nullptr) {
                        throw std::runtime_error("realloc() failed, srclang::append(), " + std::string(strerror(errno)));
                    }
                    strcat(str, str2);
                } break;
                default:
                    throw std::runtime_error("invalid append operation");
            }
            return SRCLANG_VALUE_STRING(str);
        } break;
        default:
            return SRCLANG_VALUE_ERROR(strdup(
                ("invalid append operation on '" +
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
            auto list = reinterpret_cast<SrcLangList *>(
                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            list->pop_back();

            return SRCLANG_VALUE_LIST(list);
        } break;
        case ValueType::String: {
            auto str = (char *)(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            auto len = strlen(str);
            str[len - 1] = '\0';
            return SRCLANG_VALUE_STRING(str);
        } break;
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
            case ValueType::String:
            case ValueType::Error: {
                return SRCLANG_VALUE_STRING(
                    strdup((char *)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer));
            };
            case ValueType::List: {
                auto list = reinterpret_cast<SrcLangList *>(
                    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
                auto new_list =
                    new SrcLangList(list->begin(), list->end());
                return SRCLANG_VALUE_LIST(new_list);
            };
            case ValueType::Pointer: {
                auto object = SRCLANG_VALUE_AS_OBJECT(args[0]);
                if (object->is_ref) {
                    return args[0];
                }
                auto buffer = malloc(object->size);
                if (buffer == nullptr) {
                    throw std::runtime_error("can't malloc() for srclang::clone() " + std::string(strerror(errno)));
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

SRCLANG_BUILTIN(lower) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = (char)::tolower(buffer[i]);
    }
    return SRCLANG_VALUE_STRING(newBuffer);
}

SRCLANG_BUILTIN(upper) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    char *buffer = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    char *newBuffer = strdup(buffer);

    for (int i = 0; i < strlen(buffer); i++) {
        newBuffer[i] = (char)::toupper(buffer[i]);
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
                case ValueType::String: {
                    char *val = reinterpret_cast<char *>(SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
                    char *result = strstr(buffer, val);
                    if (result == nullptr) {
                        return SRCLANG_VALUE_NUMBER(-1);
                    }
                    return SRCLANG_VALUE_NUMBER(strlen(buffer) - (result - buffer));
                } break;

                default:
                    throw std::runtime_error(
                        "can't search value '" +
                        SRCLANG_VALUE_DEBUG(args[1]) + "' in string container");
            }

        } break;

        case ValueType::List: {
            auto list = reinterpret_cast<SrcLangList *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            for (int i = 0; i < list->size(); i++) {
                if (interpreter->isEqual(args[1], list->at(i))) {
                    return SRCLANG_VALUE_NUMBER(i);
                }
            }
            return SRCLANG_VALUE_NUMBER(-1);
        } break;

        case ValueType::Map: {
            SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
            auto map = reinterpret_cast<SrcLangMap *>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            return SRCLANG_VALUE_BOOL(map->find((char *)SRCLANG_VALUE_AS_OBJECT(args[1])->pointer) != map->end());
        }
    }

    return SRCLANG_VALUE_NUMBER(-1);
}

SRCLANG_BUILTIN(eval) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    const char *buffer = (const char *)SRCLANG_VALUE_AS_OBJECT(args[0])->pointer;
    auto result = interpreter->language->execute(buffer, "<inline>");
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        // we are already registering object at language
        return builtin_clone({result}, interpreter);
    }
    return result;
}

SRCLANG_BUILTIN(alloc) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    long size = SRCLANG_VALUE_AS_NUMBER(args[0]);
    void *ptr = malloc(size * sizeof(unsigned char));
    if (ptr == nullptr) {
        std::stringstream ss;
        ss << "failed to allocate '" << size << "' size, " << strerror(errno);
        return SRCLANG_VALUE_ERROR(strdup(ss.str().c_str()));
    }
    auto value = SRCLANG_VALUE_POINTER(ptr);
    SRCLANG_VALUE_SET_SIZE(value, size);
    return value;
}

SRCLANG_BUILTIN(setref) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Boolean);

    SRCLANG_VALUE_AS_OBJECT(args[0])->is_ref = SRCLANG_VALUE_AS_BOOL(args[1]);
    return args[0];
}

SRCLANG_BUILTIN(isref) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);

    return SRCLANG_VALUE_BOOL(SRCLANG_VALUE_AS_OBJECT(args[0])->is_ref);
}

SRCLANG_BUILTIN(setsize) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);

    SRCLANG_VALUE_AS_OBJECT(args[0])->size = (size_t) SRCLANG_VALUE_AS_NUMBER(args[1]);
    return args[0];
}

SRCLANG_BUILTIN(exit) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);

    auto status = (int) SRCLANG_VALUE_AS_NUMBER(args[0]);
    interpreter->grace_full_exit();
    exit(status);
}


SRCLANG_BUILTIN(free) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Pointer);
    SRCLANG_VALUE_SET_REF(args[0]);
    SRCLANG_VALUE_SET_SIZE(args[0], 0);
    free(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
    SRCLANG_VALUE_AS_OBJECT(args[0])->pointer = nullptr;
    return SRCLANG_VALUE_TRUE;
}

SRCLANG_BUILTIN(system) {
    SRCLANG_CHECK_ARGS_RANGE(1, 2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    const char* command = (const char*) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer;

    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("popen() failed"));
    }

    Value callback = SRCLANG_VALUE_NULL;

    if (args.size() == 2) {
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Closure);
        callback = args[1];
    }

    char buffer[BUFSIZ];
    while (fgets(buffer, BUFSIZ, pipe) != nullptr) {
        if (!SRCLANG_VALUE_IS_NULL(callback)) {
            auto result = builtin_call({callback, SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(buffer))}, interpreter);
            if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
                pclose(pipe);
                return result;
            }
        }
    }
    int status = pclose(pipe);
    return SRCLANG_VALUE_NUMBER(WEXITSTATUS(status));
}

SRCLANG_BUILTIN(internalCC) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);

    const char* code = (const char*) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer;
    TCCState* state = tcc_new();
    if (state == nullptr) {
        throw std::runtime_error("tcc_new() failed");
    }
    tcc_set_output_type(state, TCC_OUTPUT_MEMORY);

    if (tcc_compile_string(state, code) == -1) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("compilation failed"));
    }

    if (tcc_relocate(state, nullptr) == -1) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("relocation failed"));
    }

    return SRCLANG_VALUE_SET_CLEANUP(SRCLANG_VALUE_OBJECT(state), SRCLANG_CLEANUP_FN(tcc_delete));
}

SRCLANG_BUILTIN(internalFFI) {
    SRCLANG_CHECK_ARGS_EXACT(4);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);
    SRCLANG_CHECK_ARGS_TYPE(2, ValueType::List);

    void* handler = nullptr;
    void* fn = nullptr;
    switch (SRCLANG_VALUE_GET_TYPE(args[3])) {
        case ValueType::String:
            handler = dlopen((const char*) SRCLANG_VALUE_AS_OBJECT(args[3])->pointer, RTLD_GLOBAL|RTLD_NOW);
            if (handler == nullptr) return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(dlerror()));
        case ValueType::Null:
            fn = dlsym(nullptr, (const char*) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            if (fn == nullptr) {
                return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(dlerror()));
            }
            break;
        
        case ValueType::Pointer:
            fn = tcc_get_symbol((TCCState*) SRCLANG_VALUE_AS_OBJECT(args[4])->pointer,(const char*) SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
            if (fn ==nullptr) {
                return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("undefined symbol"));
            }
        default:
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("expected 4th arg to be either string or pointer"));
    }

    auto args_list = (SrcLangList*) SRCLANG_VALUE_AS_OBJECT(args[2])->pointer;
    
    ffi_cif cif;
    std::vector<void*> values(args_list->size());
    std::vector<ffi_type*> types(args_list->size());

    std::vector<unsigned long> unsigned_value_holder(args_list->size());
    std::vector<signed long> signed_value_holder(args_list->size());
    std::vector<double> float_value_holder(args_list->size());

    std::vector<ffi_type*> type_map = {
        &ffi_type_sint8,
        &ffi_type_sint16,
        &ffi_type_sint32,
        &ffi_type_sint64,

        &ffi_type_uint8,
        &ffi_type_uint16,
        &ffi_type_uint32,
        &ffi_type_uint64,

        &ffi_type_float,
        &ffi_type_double,
        
        &ffi_type_pointer,
    };

    for (int i = 0; i < args_list->size(); i++) {
        if (SRCLANG_VALUE_GET_TYPE(args_list->at(i)) != ValueType::List) {
            throw std::runtime_error("expected parameters to be pair of datatype and value");
        }
        auto param = (SrcLangList*) SRCLANG_VALUE_AS_OBJECT(args_list->at(i))->pointer;
        if (param->size() != 2) {
            throw std::runtime_error("expected parameters to be pair of datatype and value");
        }
        auto datatype = (int) SRCLANG_VALUE_AS_NUMBER(param->at(0));
        types[i] = type_map[datatype];

        auto value = param->at(1);
    
        switch (datatype) {
            case 1: // i8
            case 2: // i16
            case 3: // i32
            case 4: // i64
            {
                signed_value_holder[i] = static_cast<signed long>(SRCLANG_VALUE_AS_NUMBER(value));
                values[i] = &signed_value_holder[i];
            } break;


            case 5: // u8
            case 6: // u16
            case 7: // u32
            case 8: // u64
            {
                unsigned_value_holder[i] = static_cast<unsigned long>(SRCLANG_VALUE_AS_NUMBER(value));
                values[i] = &unsigned_value_holder[i];
                types[i] = &ffi_type_uint64;
            } break;

            case 9: // f32
            case 10: // f64
            {
                float_value_holder[i] = SRCLANG_VALUE_AS_NUMBER(value);
                values[i] = &float_value_holder[i];
                types[i] = &ffi_type_double;
            } break;


            case 11: // ptr
                values[i] = &SRCLANG_VALUE_AS_OBJECT(value)->pointer;
                types[i] = &ffi_type_pointer;
                break;
        }
    }

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, args_list->size(), type_map[(int)SRCLANG_VALUE_AS_NUMBER(args[1])], types.data()) != FFI_OK) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR("ffi_prep_cif() failed"));
    }

    ffi_arg result;
    ffi_call(&cif, FFI_FN(handler), &result, values.data());

    return SRCLANG_VALUE_NUMBER(result);
}
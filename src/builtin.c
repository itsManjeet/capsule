/*
 * Copyright (c) 2024 Manjeet Singh <itsmanjeet1998@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "capsule.h"
#include "priv.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAS_FFI
#    include <dlfcn.h>
#    include <ffi.h>
#endif

#define MAX_FFI_FUN_ARGS 20

#define BUILTIN_ID(id) builtin_##id
#define BUILTIN(id) static CapsuleError BUILTIN_ID(id)(Capsule args, Capsule scope, Capsule * result)

BUILTIN(car) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (CAPSULE_NILP(CAPSULE_CAR(args)))
        *result = Capsule_nil;
    else if (CAPSULE_CAR(args).type != CAPSULE_TYPE_PAIR)
        return CAPSULE_ERROR_TYPE;
    else
        *result = CAPSULE_CAR(CAPSULE_CAR(args));

    return CAPSULE_ERROR_NONE;
}

BUILTIN(cdr) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (CAPSULE_NILP(CAPSULE_CAR(args)))
        *result = Capsule_nil;
    else if (CAPSULE_CAR(args).type != CAPSULE_TYPE_PAIR)
        return CAPSULE_ERROR_TYPE;
    else
        *result = CAPSULE_CDR(CAPSULE_CAR(args));

    return CAPSULE_ERROR_NONE;
}

BUILTIN(cons) {
    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
        return CAPSULE_ERROR_ARGS;

    *result = CAPSULE_CONS(CAPSULE_CAR(args), CAPSULE_CAR(CAPSULE_CDR(args)));

    return CAPSULE_ERROR_NONE;
}

BUILTIN(eq) {
    Capsule a, b;
    int eq;

    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
        return CAPSULE_ERROR_ARGS;

    a = CAPSULE_CAR(args);
    b = CAPSULE_CAR(CAPSULE_CDR(args));

    *result = Capsule_compare(a, b) ? CAPSULE_SYMBOL("T") : Capsule_nil;
    return CAPSULE_ERROR_NONE;
}

BUILTIN(pairp) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    *result = (CAPSULE_CAR(args).type == CAPSULE_TYPE_PAIR) ? CAPSULE_SYMBOL("T") : Capsule_nil;
    return CAPSULE_ERROR_NONE;
}

BUILTIN(procp) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    *result = (CAPSULE_CAR(args).type == CAPSULE_TYPE_BUILTIN || CAPSULE_CAR(args).type == CAPSULE_TYPE_CLOSURE) ? CAPSULE_SYMBOL("T")
                                                                                                                 : Capsule_nil;
    return CAPSULE_ERROR_NONE;
}

#define ADD_ARTHEMATIC(op, id)                                                                                      \
    BUILTIN(id) {                                                                                                   \
        if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args)))) \
            return CAPSULE_ERROR_ARGS;                                                                              \
        Capsule a = CAPSULE_CAR(args);                                                                              \
        Capsule b = CAPSULE_CAR(CAPSULE_CDR(args));                                                                 \
        if (a.type != b.type)                                                                                       \
            return CAPSULE_ERROR_TYPE;                                                                              \
        switch (a.type) {                                                                                           \
        case CAPSULE_TYPE_INTEGER:                                                                                  \
            *result = CAPSULE_INTEGER(a.as.integer op b.as.integer);                                                \
            break;                                                                                                  \
        case CAPSULE_TYPE_DECIMAL:                                                                                  \
            *result = CAPSULE_DECIMAL(a.as.decimal op b.as.decimal);                                                \
            break;                                                                                                  \
        default:                                                                                                    \
            return CAPSULE_ERROR_TYPE;                                                                              \
        }                                                                                                           \
        return CAPSULE_ERROR_NONE;                                                                                  \
    }

ADD_ARTHEMATIC(+, add)
ADD_ARTHEMATIC(-, subtract)
ADD_ARTHEMATIC(*, multiply)
ADD_ARTHEMATIC(/, divide)

#define ADD_LOGICAL(op, id)                                                                                         \
    BUILTIN(id) {                                                                                                   \
        if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args)))) \
            return CAPSULE_ERROR_ARGS;                                                                              \
        Capsule a = CAPSULE_CAR(args);                                                                              \
        Capsule b = CAPSULE_CAR(CAPSULE_CDR(args));                                                                 \
        if (a.type != b.type)                                                                                       \
            return CAPSULE_ERROR_TYPE;                                                                              \
        switch (a.type) {                                                                                           \
        case CAPSULE_TYPE_INTEGER:                                                                                  \
            *result = a.as.integer op b.as.integer ? CAPSULE_SYMBOL("T") : Capsule_nil;                             \
            break;                                                                                                  \
        case CAPSULE_TYPE_DECIMAL:                                                                                  \
            *result = a.as.decimal op b.as.decimal ? CAPSULE_SYMBOL("T") : Capsule_nil;                             \
            break;                                                                                                  \
        default:                                                                                                    \
            return CAPSULE_ERROR_TYPE;                                                                              \
        }                                                                                                           \
        return CAPSULE_ERROR_NONE;                                                                                  \
    }

ADD_LOGICAL(<, less)

BUILTIN(i2d) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    *result = CAPSULE_INTEGER(CAPSULE_CAR(args).as.decimal);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(d2i) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    *result = CAPSULE_DECIMAL(CAPSULE_CAR(args).as.integer);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(write) {
    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!(CAPSULE_POINTERP(CAPSULE_CAR(args)) && CAPSULE_STRINGP(CAPSULE_CAR(CAPSULE_CDR(args))))) {
        return CAPSULE_ERROR_TYPE;
    }

    FILE* file = CAPSULE_AS_POINTER(CAPSULE_CAR(args));
    const char* format = CAPSULE_AS_STRING(CAPSULE_CAR(CAPSULE_CDR(args)));

    args = CAPSULE_CDR(CAPSULE_CDR(args));

    while (*format) {
        if (*format == '{' && *(format + 1) == '}') {
            if (!CAPSULE_NILP(args)) {
                Capsule_print(CAPSULE_CAR(args), file);
                args = CAPSULE_CDR(args);
            }
            format += 2;
        } else {
            fputc(*format, file);
            format++;
        }
    }
    fflush(file);
    *result = Capsule_nil;
    return CAPSULE_ERROR_NONE;
}

BUILTIN(read) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_POINTERP(CAPSULE_CAR(args))) {
        return CAPSULE_ERROR_TYPE;
    }

    char buffer[BUFSIZ];
    FILE* file = CAPSULE_CAR(args).as.pointer;

    size_t read = fread(buffer, sizeof(char), sizeof(buffer), file);
    buffer[read] = '\0';

    *result = CAPSULE_STRING(strdup(buffer));
    return CAPSULE_ERROR_NONE;
}

BUILTIN(close) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_POINTERP(CAPSULE_CAR(args))) {
        return CAPSULE_ERROR_TYPE;
    }

    FILE* file = CAPSULE_CAR(args).as.pointer;

    fclose(file);
    *result = Capsule_nil;
    return CAPSULE_ERROR_NONE;
}

BUILTIN(open) {
    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_STRINGP(CAPSULE_CAR(args)) || !CAPSULE_STRINGP(CAPSULE_CAR(CAPSULE_CDR(args)))) {
        return CAPSULE_ERROR_TYPE;
    }

    const char* filepath = CAPSULE_AS_STRING(CAPSULE_CAR(args));
    const char* mode = CAPSULE_AS_STRING(CAPSULE_CAR(CAPSULE_CDR(args)));

    FILE* file = fopen(filepath, mode);
    if (file == NULL)
        return CAPSULE_ERROR_RUNTIME;

    *result = CAPSULE_POINTER(file);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(count) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (CAPSULE_STRINGP(CAPSULE_CAR(args)) || CAPSULE_SYMBOLP(args))
        *result = CAPSULE_INTEGER(strlen(CAPSULE_CAR(args).as.symbol));
    else if (CAPSULE_LISTP(CAPSULE_CAR(args))) {
        args = CAPSULE_CAR(args);
        int size = 0;
        while (!CAPSULE_NILP(args)) {
            size++;
            args = CAPSULE_CDR(args);
        }
        *result = CAPSULE_INTEGER(size);
    } else {
        return CAPSULE_ERROR_TYPE;
    }

    return CAPSULE_ERROR_NONE;
}

BUILTIN(slurp) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_STRINGP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;

    char* file = slurp(CAPSULE_CAR(args).as.symbol);
    if (file == NULL)
        return CAPSULE_ERROR_RUNTIME;

    *result = CAPSULE_STRING(file);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(eval) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;
    if (!CAPSULE_STRINGP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;

    const char* source = CAPSULE_CAR(args).as.symbol;
    return Capsule_eval(source, Capsule_Scope_global(), result);
}

BUILTIN(typeof) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;
    *result = CAPSULE_INTEGER(CAPSULE_CAR(args).type);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(popen) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_STRINGP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;

    const char* script = CAPSULE_AS_STRING(CAPSULE_CAR(args));
    FILE* process = popen(script, "r");

    *result = CAPSULE_POINTER(process);
    return CAPSULE_ERROR_NONE;
}

BUILTIN(ref) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;
    if (!CAPSULE_INTEGERP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;
    *result = CAPSULE_POINTER((void*)CAPSULE_AS_INTEGER(CAPSULE_CAR(args)));
    return CAPSULE_ERROR_NONE;
}

#ifdef HAS_FFI

static ffi_type* FFI_TYPE_MAP[] = {
    &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer, &ffi_type_sint64,
    &ffi_type_double,  &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer,
};

BUILTIN(loadlibrary) {
    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
        return CAPSULE_ERROR_ARGS;

    if (!CAPSULE_STRINGP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;

    const char* library_path = CAPSULE_AS_STRING(CAPSULE_CAR(args));
    void* handler = dlopen(library_path, RTLD_GLOBAL | RTLD_NOW);
    if (handler == NULL)
        return CAPSULE_ERROR_RUNTIME;

    *result = Capsule_managed_pointer(handler, CAPSULE_DEALLOCATOR(dlclose));
    return CAPSULE_ERROR_NONE;
}

// ;(call/cc (nil (:int (<fun> nil))))
BUILTIN(callcc) {
    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
        return CAPSULE_ERROR_ARGS;

    void* handler = NULL;
    int managed = 0;
    if (CAPSULE_STRINGP(CAPSULE_CAR(args))) {
        const char* library = CAPSULE_AS_STRING(CAPSULE_CAR(args));
        handler = dlopen(library, RTLD_NOW | RTLD_LOCAL);
        managed = 1;
        if (handler == NULL) {
            return CAPSULE_ERROR_RUNTIME;
        }
    } else if (CAPSULE_POINTERP(CAPSULE_CAR(args))) {
        handler = (CAPSULE_AS_POINTER(CAPSULE_CAR(args)));
    } else if (!CAPSULE_NILP(CAPSULE_CAR(args)))
        return CAPSULE_ERROR_TYPE;

    if (!CAPSULE_INTEGERP(CAPSULE_CAR(CAPSULE_CDR(args))) || !CAPSULE_STRINGP(CAPSULE_CAR(CAPSULE_CDR(CAPSULE_CDR(args)))))
        return CAPSULE_ERROR_TYPE;

    CapsuleType type = CAPSULE_AS_INTEGER(CAPSULE_CAR(CAPSULE_CDR(args)));
    ffi_type* return_type = FFI_TYPE_MAP[type];

    const char* function_id = CAPSULE_AS_STRING(CAPSULE_CAR(CAPSULE_CDR(CAPSULE_CDR(args))));
    void* function = dlsym(handler, function_id);

    if (function == NULL)
        return CAPSULE_ERROR_RUNTIME;

    args = CAPSULE_CDR(CAPSULE_CDR(CAPSULE_CDR(args)));
    int args_count = 0;
    ffi_type* args_types[MAX_FFI_FUN_ARGS];
    void* args_values[MAX_FFI_FUN_ARGS];
    Capsule args_holder[MAX_FFI_FUN_ARGS];

    while (!CAPSULE_NILP(args)) {
        Capsule arg = CAPSULE_CAR(args);
        args_holder[args_count] = arg;
        args_values[args_count] = &args_holder[args_count].as;
        args_types[args_count] = FFI_TYPE_MAP[arg.type];
        args_count++;
        args = CAPSULE_CDR(args);
    }

    ffi_cif cif;

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, args_count, return_type, args_types) != FFI_OK)
        return CAPSULE_ERROR_RUNTIME;

    ffi_call(&cif, FFI_FN(function), &result->as, args_values);
    result->type = type;

    if (handler && managed)
        dlclose(handler);

    return CAPSULE_ERROR_NONE;
}

#endif

void define_builtin(Capsule scope) {
#define DEFINE_VALUE(sym, value) Capsule_Scope_define(scope, CAPSULE_SYMBOL(sym), (value));
#define DEFINE_BUILTIN(sym, id) DEFINE_VALUE(sym, CAPSULE_BUILTIN(BUILTIN_ID(id)));

    DEFINE_VALUE("T", CAPSULE_SYMBOL("T"))

    DEFINE_VALUE("STDOUT", CAPSULE_POINTER(stdout));
    DEFINE_VALUE("STDERR", CAPSULE_POINTER(stderr));
    DEFINE_VALUE("STDIN", CAPSULE_POINTER(stdin));

    DEFINE_BUILTIN("CAR", car);
    DEFINE_BUILTIN("CDR", cdr);
    DEFINE_BUILTIN("CONS", cons);
    DEFINE_BUILTIN("+", add);
    DEFINE_BUILTIN("-", subtract);
    DEFINE_BUILTIN("*", multiply);
    DEFINE_BUILTIN("/", divide);

    DEFINE_BUILTIN("<", less);
    DEFINE_BUILTIN("EQ?", eq);
    DEFINE_BUILTIN("PAIR?", pairp);
    DEFINE_BUILTIN("PROCEDURE?", procp);

    DEFINE_BUILTIN("REF", ref)
    DEFINE_BUILTIN("WRITE", write)
    DEFINE_BUILTIN("READ", read)
    DEFINE_BUILTIN("OPEN/PROCESS", popen)
    DEFINE_BUILTIN("OPEN", open)
    DEFINE_BUILTIN("CLOSE", close)
    DEFINE_BUILTIN("COUNT", count);
    DEFINE_BUILTIN("SLURP", slurp);
    DEFINE_BUILTIN("EVAL", eval);
    DEFINE_BUILTIN("TYPEOF", typeof);

    DEFINE_BUILTIN("INT->DEC", i2d)
    DEFINE_BUILTIN("DEC->INT", d2i)

    DEFINE_VALUE(":INT", CAPSULE_INTEGER(CAPSULE_TYPE_INTEGER));
    DEFINE_VALUE(":DEC", CAPSULE_INTEGER(CAPSULE_TYPE_DECIMAL));
    DEFINE_VALUE(":STR", CAPSULE_INTEGER(CAPSULE_TYPE_STRING));
    DEFINE_VALUE(":SYM", CAPSULE_INTEGER(CAPSULE_TYPE_SYMBOL));
    DEFINE_VALUE(":PTR", CAPSULE_INTEGER(CAPSULE_TYPE_POINTER));

#ifdef HAS_FFI
    DEFINE_BUILTIN("CALL/CC", callcc)
    DEFINE_BUILTIN("LOAD-LIBRARY", loadlibrary)
#endif
}
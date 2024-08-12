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

#ifndef CAPSULE_H
#define CAPSULE_H
#include <stdio.h>

typedef enum {
    CAPSULE_ERROR_NONE = 0,
    CAPSULE_ERROR_SYNTAX,
    CAPSULE_ERROR_UNBOUND,
    CAPSULE_ERROR_ARGS,
    CAPSULE_ERROR_TYPE,
    CAPSULE_ERROR_RUNTIME,
} CapsuleError;

struct Capsule;

typedef CapsuleError (*CapsuleBuiltin)(struct Capsule args, struct Capsule scope, struct Capsule* result);

typedef enum {
    CAPSULE_TYPE_NIL,
    CAPSULE_TYPE_PAIR,
    CAPSULE_TYPE_SYMBOL,
    CAPSULE_TYPE_STRING,
    CAPSULE_TYPE_INTEGER,
    CAPSULE_TYPE_DECIMAL,
    CAPSULE_TYPE_POINTER,
    CAPSULE_TYPE_BUILTIN,
    CAPSULE_TYPE_CLOSURE,
    CAPSULE_TYPE_MACRO,
} CapsuleType;

struct Capsule {
    CapsuleType type;

    union {
        struct CapsulePair* pair;
        const char* symbol;
        long integer;
        double decimal;
        CapsuleBuiltin builtin;
        void* pointer;
    } as;
};

struct CapsulePair {
    struct Capsule pellete[2];
};

typedef struct Capsule Capsule;

#define CAPSULE_CAR(cap) ((cap).as.pair->pellete[0])
#define CAPSULE_CDR(cap) ((cap).as.pair->pellete[1])

#define CAPSULE_INTEGER(x) ((Capsule){.type = CAPSULE_TYPE_INTEGER, .as.integer = (x)})
#define CAPSULE_DECIMAL(x) ((Capsule){.type = CAPSULE_TYPE_DECIMAL, .as.decimal = (x)})
#define CAPSULE_STRING(x) Capsule_String_new(x)
#define CAPSULE_SYMBOL(x) Capsule_Symbol_new(x)
#define CAPSULE_BUILTIN(x) ((Capsule){.type = CAPSULE_TYPE_BUILTIN, .as.builtin = (x)})
#define CAPSULE_POINTER(x) ((Capsule){.type = CAPSULE_TYPE_POINTER, .as.pointer = (x)})

#define CAPSULE_CONS(car, cdr) (Capsule_cons((car), (cdr)))

#define CAPSULE_AS_STRING(cap) ((cap).as.symbol)
#define CAPSULE_AS_INTEGER(cap) ((cap).as.integer)
#define CAPSULE_AS_DECIMAL(cap) ((cap).as.decimal)
#define CAPSULE_AS_SYMBOL(cap) ((cap).as.symbol)
#define CAPSULE_AS_POINTER(cap) ((cap).as.pointer)

#define CAPSULE_NILP(cap) ((cap).type == CAPSULE_TYPE_NIL)
#define CAPSULE_INTEGERP(cap) ((cap).type == CAPSULE_TYPE_INTEGER)
#define CAPSULE_DECIMALP(cap) ((cap).type == CAPSULE_TYPE_DECIMAL)
#define CAPSULE_STRINGP(cap) ((cap).type == CAPSULE_TYPE_STRING)
#define CAPSULE_SYMBOLP(cap) ((cap).type == CAPSULE_TYPE_SYMBOL)
#define CAPSULE_POINTERP(cap) ((cap).type == CAPSULE_TYPE_POINTER)
#define CAPSULE_LISTP(cap) Capsule_Listp(cap)

#define CAPSULE_SYMBOL_COMPARE(cap, str) Capsule_Symbol_compare(cap, str)

static const Capsule Capsule_nil = {CAPSULE_TYPE_NIL};

CapsuleError Capsule_read(const char* source, Capsule* result);

void Capsule_print(Capsule atom, FILE* out);

const char* Capsule_logo();

CapsuleError Capsule_eval_cap(Capsule expr, Capsule scope, Capsule* result);

CapsuleError Capsule_eval(const char* source, Capsule scope, Capsule* result);

Capsule Capsule_managed_pointer(void* pointer, void (*dellocate)(void*));

int Capsule_compare(Capsule a, Capsule b);

const char* Capsule_Error_str(CapsuleError error);

Capsule Capsule_Scope_new(Capsule parent);

Capsule Capsule_Scope_global();

int Capsule_Scope_define(Capsule env, Capsule symbol, Capsule value);

int Capsule_Scope_lookup(Capsule env, Capsule symbol, Capsule* result);

int Capsule_Scope_set(Capsule env, Capsule symbol, Capsule value);

Capsule Capsule_cons(Capsule car_val, Capsule cdr_val);

Capsule Capsule_Symbol_new(const char* s);

Capsule Capsule_String_new(const char* str);

int Capsule_Symbol_compare(Capsule cap, const char* s);

int Capsule_Listp(Capsule expr);

Capsule Capsule_List_clone(Capsule list);

Capsule Capsule_List_new(int n, ...);

Capsule Capsule_List_at(Capsule list, int k);

void Capsule_List_set(Capsule list, int k, Capsule value);

void Capsule_List_reverse(Capsule* list);

#endif
#ifndef CAPSULE_H
#define CAPSULE_H

#include <stdio.h>

typedef enum {
    CapsuleError_None = 0,
    CapsuleError_Syntax,
    CapsuleError_Unbound,
    CapsuleError_InvalidArgs,
    CapsuleError_InvalidType
} CapsuleError;

struct Capsule;

typedef struct Capsule (*CapsuleBuiltin)(struct Capsule args, struct Capsule scope);

typedef enum CapsuleType {
    CapsuleType_Nil,
    CapsuleType_Pair,
    CapsuleType_Symbol,
    CapsuleType_Integer,
    CapsuleType_Builtin,
    CapsuleType_Closure,
    CapsuleType_Macro,
    CapsuleType_Error,
} CapsuleType;

struct Capsule {
    CapsuleType type;
    union {
        struct Pair *pair;
        const char *symbol;
        long integer;
        CapsuleBuiltin builtin;
        CapsuleError error;
    } as;
    const char *position;
};

struct Pair {
    struct Capsule pellet[2];
};

typedef struct Capsule Capsule;

#define CAPSULE_CAR(p) ((p).as.pair->pellet[0])
#define CAPSULE_CDR(p) ((p).as.pair->pellet[1])
#define CAPSULE_NILP(atom) ((atom).type == CapsuleType_Nil)
#define CAPSULE_ERRORP(cap) ((cap).type == CapsuleType_Error)
#define CAPSULE_CONS(a, b) Capsule_cons((a), (b))

#define CAPSULE_INT(v) ((Capsule){.type = CapsuleType_Integer, .as.integer = (v), .position = NULL})
#define CAPSULE_BUILTIN(v) ((Capsule){.type = CapsuleType_Builtin, .as.builtin = (v), .position = NULL})
#define CAPSULE_ERROR(v) ((Capsule){.type = CapsuleType_Error, .as.error = (v), .position = NULL})
#define CAPSULE_ERROR_POS(v, p) ((Capsule){.type = CapsuleType_Error, .as.error = (v), .position = (p)})
#define CAPSULE_SYMBOL(v) Capsule_symbol_new((v))

#define CAPSULE_POSITION(cap) ((cap).position)

#define CAPSULE_AS_ERROR(cap) ((cap).as.error)

static const Capsule Capsule_nil = {CapsuleType_Nil};

const char* CapsuleError_str(Capsule error);

Capsule Capsule_read(const char *source);

void Capsule_print(Capsule capsule, FILE *out);

Capsule Capsule_scope_new(Capsule parent);
int Capsule_scope_define(Capsule scope, Capsule symbol, Capsule as);
Capsule Capsule_scope_lookup(Capsule scope, Capsule symbol);
int Capsule_scope_set(Capsule scope, Capsule symbol, Capsule as);

Capsule Capsule_eval_expr(Capsule expr, Capsule scope);
Capsule Capsule_eval(const char *source, Capsule scope);

Capsule Capsule_cons(Capsule car_val, Capsule cdr_val);

Capsule Capsule_symbol_new(const char *s);

int Capsule_listp(Capsule expr);
Capsule Capsule_list_clone(Capsule list);
Capsule Capsule_list_new(int n, ...);
Capsule Capsule_list_at(Capsule list, int k);
void Capsule_list_set(Capsule list, int k, Capsule as);
void Capsule_list_reverse(Capsule *list);

#endif
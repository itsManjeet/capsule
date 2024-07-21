#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "_priv.h"

struct Allocation {
    struct Pair pair;
    int mark: 1;
    struct Allocation *next;
};

struct Allocation *global_allocations = NULL;

Capsule Capsule_cons(Capsule car_val, Capsule cdr_val) {
    struct Allocation *a;
    Capsule p;

    a = malloc(sizeof(struct Allocation));
    a->mark = 0;
    a->next = global_allocations;
    global_allocations = a;

    p.type = CapsuleType_Pair;
    p.as.pair = &a->pair;

    CAPSULE_CAR(p) = car_val;
    CAPSULE_CDR(p) = cdr_val;

    return p;
}

static Capsule sym_table = {CapsuleType_Nil};

Capsule Capsule_symbol_new(const char *s) {
    Capsule a, p;

    p = sym_table;
    while (!CAPSULE_NILP(p)) {
        a = CAPSULE_CAR(p);
        if (strcmp(a.as.symbol, s) == 0)
            return a;
        p = CAPSULE_CDR(p);
    }

    a.type = CapsuleType_Symbol;
    a.as.symbol = strdup(s);
    sym_table = Capsule_cons(a, sym_table);

    return a;
}

int Capsule_listp(Capsule expr) {
    while (!CAPSULE_NILP(expr)) {
        if (expr.type != CapsuleType_Pair)
            return 0;
        expr = CAPSULE_CDR(expr);
    }
    return 1;
}

Capsule Capsule_list_clone(Capsule list) {
    Capsule a, p;

    if (CAPSULE_NILP(list))
        return Capsule_nil;

    a = Capsule_cons(CAPSULE_CAR(list), Capsule_nil);
    p = a;
    list = CAPSULE_CDR(list);

    while (!CAPSULE_NILP(list)) {
        CAPSULE_CDR(p) = Capsule_cons(CAPSULE_CAR(list), Capsule_nil);
        p = CAPSULE_CDR(p);
        list = CAPSULE_CDR(list);
    }

    return a;
}

Capsule Capsule_list_new(int n, ...) {
    va_list ap;
    Capsule list = Capsule_nil;

            va_start(ap, n);
    while (n--) {
        Capsule item = va_arg(ap, Capsule);
        list = Capsule_cons(item, list);
    }
            va_end(ap);

    Capsule_list_reverse(&list);
    return list;
}

Capsule Capsule_list_at(Capsule list, int k) {
    while (k--)
        list = CAPSULE_CDR(list);
    return CAPSULE_CAR(list);
}

void Capsule_list_set(Capsule list, int k, Capsule value) {
    while (k--)
        list = CAPSULE_CDR(list);
    CAPSULE_CAR(list) = value;
}

void Capsule_list_reverse(Capsule *list) {
    Capsule tail = Capsule_nil;
    while (!CAPSULE_NILP(*list)) {
        Capsule p = CAPSULE_CDR(*list);
        CAPSULE_CDR(*list) = tail;
        tail = *list;
        *list = p;
    }
    *list = tail;
}

void gc_mark(Capsule root) {
    struct Allocation *a;

    if (!(root.type == CapsuleType_Pair || root.type == CapsuleType_Closure || root.type == CapsuleType_Macro))
        return;

    a = (struct Allocation *) ((char *) root.as.pair - offsetof(struct Allocation, pair));

    if (a->mark)
        return;

    a->mark = 1;

    gc_mark(CAPSULE_CAR(root));
    gc_mark(CAPSULE_CDR(root));
}

void gc() {
    struct Allocation *a, **p;

    gc_mark(sym_table);

    p = &global_allocations;
    while (*p != NULL) {
        a = *p;
        if (!a->mark) {
            *p = a->next;
            free(a);
        } else {
            p = &a->next;
        }
    }

    a = global_allocations;
    while (a != NULL) {
        a->mark = 0;
        a = a->next;
    }
}

void Capsule_print(Capsule pellet, FILE *out) {
    switch (pellet.type) {
        case CapsuleType_Nil:
            fprintf(out, "NIL");
            break;
        case CapsuleType_Pair:
            fputc('(', out);
            Capsule_print(CAPSULE_CAR(pellet), out);
            pellet = CAPSULE_CDR(pellet);
            while (!CAPSULE_NILP(pellet)) {
                if (pellet.type == CapsuleType_Pair) {
                    fputc(' ', out);
                    Capsule_print(CAPSULE_CAR(pellet), out);
                    pellet = CAPSULE_CDR(pellet);
                } else {
                    fprintf(out, " . ");
                    Capsule_print(pellet, out);
                    break;
                }
            }
            fputc(')', out);
            break;
        case CapsuleType_Symbol:
            fprintf(out, "%s", pellet.as.symbol);
            break;
        case CapsuleType_String:
            fprintf(out, "%s", pellet.as.symbol);
            break;
        case CapsuleType_Integer:
            fprintf(out, "%ld", pellet.as.integer);
            break;
        case CapsuleType_Builtin:
            fprintf(out, "#<BUILTIN:%p>", pellet.as.builtin);
            break;
        case CapsuleType_Closure:
            fprintf(out, "#<CLOSURE:%p>", pellet.as.pair);
            break;
        case CapsuleType_Macro:
            fprintf(out, "#<MACRO:%p>", pellet.as.pair);
            break;
        case CapsuleType_Error: {
            fprintf(out, "#<ERROR:%d>", CAPSULE_AS_ERROR(pellet));
        }
            break;
        default:
            fprintf(out, "#<VALUE:%d>", pellet.type);
            break;
    }
}

const char *CapsuleError_str(Capsule error) {
    switch (CAPSULE_AS_ERROR(error)) {
        case CapsuleError_None:
            return "None";
        case CapsuleError_Syntax:
            return "Invalid Syntax";
        case CapsuleError_InvalidType:
            return "Unexpected Capsule Type";
        case CapsuleError_Unbound:
            return "Unbounded Capsule";
        case CapsuleError_InvalidArgs:
            return "Unexpected arguments";
    }
}
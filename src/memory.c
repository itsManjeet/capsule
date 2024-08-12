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

#include "priv.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct Allocation {
    int mark;
    CapsuleType type;
    struct Allocation* next;
    void (*deallocate)(void*);
    void* pointer;
} Allocation;

static Allocation* global_allocations = NULL;

Allocation* Capsule_alloc(CapsuleType type, size_t size, void (*deallocate)(void*)) {
    Allocation* alloc = malloc(sizeof(Allocation) + size);
    alloc->mark = 0;
    alloc->type = type;
    alloc->next = global_allocations;
    alloc->deallocate = deallocate;
    global_allocations = alloc;

    alloc->pointer = alloc + 1;
    return alloc;
}

Capsule Capsule_cons(Capsule car_val, Capsule cdr_val) {
    Allocation* alloc = Capsule_alloc(CAPSULE_TYPE_PAIR, sizeof(struct CapsulePair), free);

    Capsule pair = {.type = CAPSULE_TYPE_PAIR, .as.pair = alloc->pointer};
    CAPSULE_CAR(pair) = car_val;
    CAPSULE_CDR(pair) = cdr_val;

    return pair;
}

Capsule Capsule_managed_pointer(void* pointer, void (*dellocate)(void*)) {
    Allocation* alloc = Capsule_alloc(CAPSULE_TYPE_POINTER, sizeof(void*), dellocate);
    Capsule cap = {.type = CAPSULE_TYPE_POINTER, .as.pointer = alloc->pointer = pointer};
    return cap;
}

Capsule Capsule_String_new(const char* str) {
    size_t size = strlen(str);
    Allocation* alloc = Capsule_alloc(CAPSULE_TYPE_STRING, sizeof(char) * (size + 1), free);

    Capsule string = {
            .type = CAPSULE_TYPE_STRING,
            .as.symbol = alloc->pointer,
    };

    memcpy(alloc->pointer, str, size);
    char* buffer = alloc->pointer;
    buffer[size] = '\0';
    return string;
}

static Capsule sym_table = {CAPSULE_TYPE_NIL};

Capsule Capsule_Symbol_new(const char* s) {
    Capsule a, p;

    p = sym_table;
    while (!CAPSULE_NILP(p)) {
        a = CAPSULE_CAR(p);
        if (strcmp(a.as.symbol, s) == 0) return a;
        p = CAPSULE_CDR(p);
    }

    a = Capsule_String_new(s);
    a.type = CAPSULE_TYPE_SYMBOL;
    sym_table = CAPSULE_CONS(a, sym_table);

    return a;
}

void gc_mark(Capsule root) {

    Allocation* alloc;

    switch (root.type) {
    case CAPSULE_TYPE_PAIR:
    case CAPSULE_TYPE_MACRO:
    case CAPSULE_TYPE_CLOSURE: alloc = (Allocation*)(root.as.pair) - 1; break;
    case CAPSULE_TYPE_STRING:
    case CAPSULE_TYPE_SYMBOL: alloc = (Allocation*)(root.as.symbol) - 1; break;
    case CAPSULE_TYPE_POINTER: alloc = (Allocation*)(root.as.pointer) - 1; break;
    default: return;
    }

    if (alloc->mark) return;

#ifdef DEBUG_GC
    fprintf(stdout, "marking");
    Capsule_print(root, stdout);
    fprintf(stdout, "\n");
#endif

    alloc->mark = 1;
    switch (root.type) {
    case CAPSULE_TYPE_PAIR:
    case CAPSULE_TYPE_MACRO:
    case CAPSULE_TYPE_CLOSURE:
        gc_mark(CAPSULE_CAR(root));
        gc_mark(CAPSULE_CDR(root));
        break;
    default: break;
    }
}

#ifdef DEBUG_GC
static void print_allocations() {
    Allocation* a = global_allocations;

    if (a == NULL) {
        printf("No allocations found.\n");
        return;
    }

    printf("Current allocations:\n");

    while (a != NULL) {
        printf("ADDRESS : %p\n", a);
        printf("VALUE   : ");
        switch (a->type) {
        case CAPSULE_TYPE_PAIR:
        case CAPSULE_TYPE_MACRO:
        case CAPSULE_TYPE_CLOSURE: Capsule_print((Capsule){.type = a->type, .as.pair = a->pointer}, stdout); break;
        case CAPSULE_TYPE_STRING:
        case CAPSULE_TYPE_SYMBOL: Capsule_print((Capsule){.type = a->type, .as.symbol = a->pointer}, stdout); break;
        case CAPSULE_TYPE_POINTER: Capsule_print((Capsule){.type = a->type, .as.pointer = a->pointer}, stdout); break;
        default: printf("Unknown type %d", a->type);
        }

        printf("\nMARK    : %d\n", a->mark);
        printf("POINTER : %p\n", a->pointer);
        printf("NEXT    : %p\n\n", (void*)a->next);

        a = a->next;
    }
}
#endif

void gc() {
    Allocation *a = NULL, *prev = NULL, **p = NULL;

    gc_mark(sym_table);
#ifdef DEBUG_GC
    // print_allocations();
#endif
    p = &global_allocations;
    while (*p != NULL) {
        prev = NULL;
        a = *p;
        if (!a->mark) {
            *p = a->next;

#ifdef DEBUG_GC
            fprintf(stdout, "deallocating: ");
            switch (a->type) {
            case CAPSULE_TYPE_PAIR:
            case CAPSULE_TYPE_MACRO:
            case CAPSULE_TYPE_CLOSURE: Capsule_print((Capsule){.type = a->type, .as.pair = a->pointer}, stdout); break;
            case CAPSULE_TYPE_STRING:
            case CAPSULE_TYPE_SYMBOL: Capsule_print((Capsule){.type = a->type, .as.symbol = a->pointer}, stdout); break;
            case CAPSULE_TYPE_POINTER:
                Capsule_print((Capsule){.type = a->type, .as.pointer = a->pointer}, stdout);
                break;
            default: fprintf(stdout, "invalid type %d", a->type);
            }
            fprintf(stdout, "\n");
#endif
            void (*deallocate)(void*) = a->deallocate;
            deallocate(a);
        } else {
            prev = a;
            p = &a->next;
        }
    }

    if (prev != NULL) { prev->next = NULL; }

    a = global_allocations;
    while (a != NULL) {
        a->mark = 0;
        a = a->next;
    }
}

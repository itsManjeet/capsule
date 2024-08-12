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
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

int Capsule_Symbol_compare(Capsule cap, const char* s) {
    return strcmp(cap.as.symbol, s) == 0;
}

int Capsule_Listp(Capsule expr) {
    while (!CAPSULE_NILP(expr)) {
        if (expr.type != CAPSULE_TYPE_PAIR) {
            return 0;
        }
        expr = CAPSULE_CDR(expr);
    }
    return 1;
}

Capsule Capsule_List_clone(Capsule list) {
    Capsule a, p;

    if (CAPSULE_NILP(list)) {
        return Capsule_nil;
    }

    a = CAPSULE_CONS(CAPSULE_CAR(list), Capsule_nil);
    p = a;
    list = CAPSULE_CDR(list);

    while (!CAPSULE_NILP(list)) {
        CAPSULE_CDR(p) = CAPSULE_CONS(CAPSULE_CAR(list), Capsule_nil);
        p = CAPSULE_CDR(p);
        list = CAPSULE_CDR(list);
    }

    return a;
}

Capsule Capsule_List_new(int n, ...) {
    va_list ap;
    Capsule list = Capsule_nil;

    va_start(ap, n);
    while (n--) {
        Capsule item = va_arg(ap, Capsule);
        list = CAPSULE_CONS(item, list);
    }
    va_end(ap);

    Capsule_List_reverse(&list);
    return list;
}

Capsule Capsule_List_at(Capsule list, int k) {
    while (k--) {
        list = CAPSULE_CDR(list);
    }
    return CAPSULE_CAR(list);
}

void Capsule_List_set(Capsule list, int k, Capsule value) {
    while (k--) {
        list = CAPSULE_CDR(list);
    }
    CAPSULE_CAR(list) = value;
}

void Capsule_List_reverse(Capsule* list) {
    Capsule tail = Capsule_nil;
    while (!CAPSULE_NILP(*list)) {
        Capsule p = CAPSULE_CDR(*list);
        CAPSULE_CDR(*list) = tail;
        tail = *list;
        *list = p;
    }
    *list = tail;
}

int Capsule_compare(Capsule a, Capsule b) {
    if (a.type != b.type) {
        return 0;
    }

    switch (a.type) {
    case CAPSULE_TYPE_NIL:
        return 1;
    case CAPSULE_TYPE_SYMBOL:
        return a.as.symbol == b.as.symbol;
    case CAPSULE_TYPE_INTEGER:
        return a.as.integer == b.as.integer;
    case CAPSULE_TYPE_DECIMAL:
        return a.as.decimal == b.as.decimal;
    case CAPSULE_TYPE_BUILTIN:
        return a.as.builtin == b.as.builtin;
    case CAPSULE_TYPE_STRING:
        return strcmp(a.as.symbol, b.as.symbol) == 0;
    case CAPSULE_TYPE_POINTER:
        return (uintptr_t)a.as.pointer == (uintptr_t)b.as.pointer;
    case CAPSULE_TYPE_MACRO:
    case CAPSULE_TYPE_CLOSURE:
        return a.as.pair == b.as.pair;
    case CAPSULE_TYPE_PAIR: {
        while (CAPSULE_NILP(a)) {
            if (CAPSULE_NILP(b)) {
                return 0;
            }
            if (!Capsule_compare(a, b)) {
                return 0;
            }

            a = CAPSULE_CDR(a);
            b = CAPSULE_CDR(b);
        }
        if (!CAPSULE_NILP(b)) {
            return 0;
        }
        return 1;
    }
    }
    return 0;
}

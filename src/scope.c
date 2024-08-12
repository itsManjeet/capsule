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
#include "runtime.h"
#include <stdio.h>

static Capsule global_scope = {CAPSULE_TYPE_NIL};

Capsule Capsule_Scope_global() {
    if (CAPSULE_NILP(global_scope)) {
        Capsule capsule;
        CapsuleError error;
        global_scope = Capsule_Scope_new(Capsule_nil);
        define_builtin(global_scope);

        if ((error = Capsule_eval(RUNTIME, global_scope, &capsule))) {
            fprintf(stderr, "ERROR: failed to load runtime, skipping: %s\n", Capsule_Error_str(error));
        }
    }
    return global_scope;
}

Capsule Capsule_Scope_new(Capsule parent) {
    return CAPSULE_CONS(parent, Capsule_nil);
}

int Capsule_Scope_define(Capsule env, Capsule symbol, Capsule value) {
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            CAPSULE_CDR(b) = value;
            return CAPSULE_ERROR_NONE;
        }
        bs = CAPSULE_CDR(bs);
    }

    CAPSULE_CDR(env) = CAPSULE_CONS(CAPSULE_CONS(symbol, value), CAPSULE_CDR(env));

    return CAPSULE_ERROR_NONE;
}

int Capsule_Scope_lookup(Capsule env, Capsule symbol, Capsule* result) {
    Capsule parent = CAPSULE_CAR(env);
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            *result = CAPSULE_CDR(b);
            return CAPSULE_ERROR_NONE;
        }
        bs = CAPSULE_CDR(bs);
    }

    if (CAPSULE_NILP(parent))
        return CAPSULE_ERROR_UNBOUND;

    return Capsule_Scope_lookup(parent, symbol, result);
}

int Capsule_Scope_set(Capsule env, Capsule symbol, Capsule value) {
    Capsule parent = CAPSULE_CAR(env);
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            CAPSULE_CDR(b) = value;
            return CAPSULE_ERROR_NONE;
        }
        bs = CAPSULE_CDR(bs);
    }

    if (CAPSULE_NILP(parent))
        return CAPSULE_ERROR_UNBOUND;

    return Capsule_Scope_set(parent, symbol, value);
}
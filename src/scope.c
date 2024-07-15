#include "_priv.h"

Capsule Capsule_scope_new(Capsule parent) {
    return Capsule_cons(parent, Capsule_nil);
}

int Capsule_scope_define(Capsule env, Capsule symbol, Capsule as) {
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            CAPSULE_CDR(b) = as;
            return CapsuleError_None;
        }
        bs = CAPSULE_CDR(bs);
    }

    CAPSULE_CDR(env) = Capsule_cons(Capsule_cons(symbol, as), CAPSULE_CDR(env));

    return CapsuleError_None;
}

Capsule Capsule_scope_lookup(Capsule env, Capsule symbol) {
    Capsule parent = CAPSULE_CAR(env);
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            return CAPSULE_CDR(b);
        }
        bs = CAPSULE_CDR(bs);
    }

    if (CAPSULE_NILP(parent))
        return CAPSULE_ERROR_POS(CapsuleError_Unbound, CAPSULE_POSITION(symbol));

    return Capsule_scope_lookup(parent, symbol);
}

int Capsule_scope_set(Capsule env, Capsule symbol, Capsule as) {
    Capsule parent = CAPSULE_CAR(env);
    Capsule bs = CAPSULE_CDR(env);

    while (!CAPSULE_NILP(bs)) {
        Capsule b = CAPSULE_CAR(bs);
        if (CAPSULE_CAR(b).as.symbol == symbol.as.symbol) {
            CAPSULE_CDR(b) = as;
            return CapsuleError_None;
        }
        bs = CAPSULE_CDR(bs);
    }

    if (CAPSULE_NILP(parent))
        return CapsuleError_Unbound;

    return Capsule_scope_set(parent, symbol, as);
}
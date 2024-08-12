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
#include <stdio.h>
#include <string.h>

#ifdef STRESS_GC
#    define GC_THRESHOLD 1
#else
#    define GC_THRESHOLD 1000
#endif

static int make_closure(Capsule env, Capsule args, Capsule body, Capsule* result) {
    Capsule p;

    if (!CAPSULE_LISTP(body)) return CAPSULE_ERROR_SYNTAX;

    p = args;
    while (!CAPSULE_NILP(p)) {
        if (p.type == CAPSULE_TYPE_SYMBOL)
            break;
        else if (p.type != CAPSULE_TYPE_PAIR || CAPSULE_CAR(p).type != CAPSULE_TYPE_SYMBOL)
            return CAPSULE_ERROR_TYPE;
        p = CAPSULE_CDR(p);
    }

    *result = CAPSULE_CONS(env, CAPSULE_CONS(args, body));
    result->type = CAPSULE_TYPE_CLOSURE;

    return CAPSULE_ERROR_NONE;
}

static Capsule make_frame(Capsule parent, Capsule env, Capsule tail) {
    return CAPSULE_CONS(parent,
            CAPSULE_CONS(env,
                    CAPSULE_CONS(Capsule_nil,
                            CAPSULE_CONS(tail, CAPSULE_CONS(Capsule_nil, CAPSULE_CONS(Capsule_nil, Capsule_nil))))));
}

static int eval_do_exec(Capsule* stack, Capsule* expr, Capsule* env) {
    Capsule body;

    *env = Capsule_List_at(*stack, 1);
    body = Capsule_List_at(*stack, 5);
    *expr = CAPSULE_CAR(body);
    body = CAPSULE_CDR(body);
    if (CAPSULE_NILP(body)) {

        *stack = CAPSULE_CAR(*stack);
    } else {
        Capsule_List_set(*stack, 5, body);
    }

    return CAPSULE_ERROR_NONE;
}

static int eval_do_bind(Capsule* stack, Capsule* expr, Capsule* env) {
    Capsule op, args, arg_names, body;

    body = Capsule_List_at(*stack, 5);
    if (!CAPSULE_NILP(body)) return eval_do_exec(stack, expr, env);

    op = Capsule_List_at(*stack, 2);
    args = Capsule_List_at(*stack, 4);

    *env = Capsule_Scope_new(CAPSULE_CAR(op));
    arg_names = CAPSULE_CAR(CAPSULE_CDR(op));
    body = CAPSULE_CDR(CAPSULE_CDR(op));
    Capsule_List_set(*stack, 1, *env);
    Capsule_List_set(*stack, 5, body);

    while (!CAPSULE_NILP(arg_names)) {
        if (arg_names.type == CAPSULE_TYPE_SYMBOL) {
            Capsule_Scope_define(*env, arg_names, args);
            args = Capsule_nil;
            break;
        }

        if (CAPSULE_NILP(args)) return CAPSULE_ERROR_ARGS;
        Capsule_Scope_define(*env, CAPSULE_CAR(arg_names), CAPSULE_CAR(args));
        arg_names = CAPSULE_CDR(arg_names);
        args = CAPSULE_CDR(args);
    }
    if (!CAPSULE_NILP(args)) return CAPSULE_ERROR_ARGS;

    Capsule_List_set(*stack, 4, Capsule_nil);

    return eval_do_exec(stack, expr, env);
}

static int eval_do_apply(Capsule* stack, Capsule* expr, Capsule* env, Capsule* result) {
    Capsule op, args;

    op = Capsule_List_at(*stack, 2);
    args = Capsule_List_at(*stack, 4);

    if (!CAPSULE_NILP(args)) {
        Capsule_List_reverse(&args);
        Capsule_List_set(*stack, 4, args);
    }

    if (op.type == CAPSULE_TYPE_SYMBOL) {
        if (strcmp(op.as.symbol, "APPLY") == 0) {

            *stack = CAPSULE_CAR(*stack);
            *stack = make_frame(*stack, *env, Capsule_nil);
            op = CAPSULE_CAR(args);
            args = CAPSULE_CAR(CAPSULE_CDR(args));
            if (!CAPSULE_LISTP(args)) return CAPSULE_ERROR_SYNTAX;

            Capsule_List_set(*stack, 2, op);
            Capsule_List_set(*stack, 4, args);
        }
    }

    if (op.type == CAPSULE_TYPE_BUILTIN) {
        *stack = CAPSULE_CAR(*stack);
        *expr = CAPSULE_CONS(op, args);
        return CAPSULE_ERROR_NONE;
    } else if (op.type != CAPSULE_TYPE_CLOSURE) {
        return CAPSULE_ERROR_TYPE;
    }

    return eval_do_bind(stack, expr, env);
}

static int eval_do_return(Capsule* stack, Capsule* expr, Capsule* env, Capsule* result) {
    Capsule op, args, body;

    *env = Capsule_List_at(*stack, 1);
    op = Capsule_List_at(*stack, 2);
    body = Capsule_List_at(*stack, 5);

    if (!CAPSULE_NILP(body)) { return eval_do_apply(stack, expr, env, result); }

    if (CAPSULE_NILP(op)) {

        op = *result;
        Capsule_List_set(*stack, 2, op);

        if (op.type == CAPSULE_TYPE_MACRO) {

            args = Capsule_List_at(*stack, 3);
            *stack = make_frame(*stack, *env, Capsule_nil);
            op.type = CAPSULE_TYPE_CLOSURE;
            Capsule_List_set(*stack, 2, op);
            Capsule_List_set(*stack, 4, args);
            return eval_do_bind(stack, expr, env);
        }
    } else if (op.type == CAPSULE_TYPE_SYMBOL) {

        if (strcmp(op.as.symbol, "DEFINE") == 0) {
            Capsule sym = Capsule_List_at(*stack, 4);
            (void)Capsule_Scope_define(*env, sym, *result);
            *stack = CAPSULE_CAR(*stack);
            *expr = CAPSULE_CONS(CAPSULE_SYMBOL("QUOTE"), CAPSULE_CONS(sym, Capsule_nil));
            return CAPSULE_ERROR_NONE;
        } else if (strcmp(op.as.symbol, "SET!") == 0) {
            Capsule sym = Capsule_List_at(*stack, 4);
            *stack = CAPSULE_CAR(*stack);
            *expr = CAPSULE_CONS(CAPSULE_SYMBOL("QUOTE"), CAPSULE_CONS(sym, Capsule_nil));
            return Capsule_Scope_set(*env, sym, *result);
        } else if (strcmp(op.as.symbol, "IF") == 0) {
            args = Capsule_List_at(*stack, 3);
            *expr = CAPSULE_NILP(*result) ? CAPSULE_CAR(CAPSULE_CDR(args)) : CAPSULE_CAR(args);
            *stack = CAPSULE_CAR(*stack);
            return CAPSULE_ERROR_NONE;
        } else if (CAPSULE_SYMBOL_COMPARE(op, "BEGIN")) {
            args = Capsule_List_at(*stack, 3);
            *expr = CAPSULE_CONS(CAPSULE_SYMBOL("BEGIN"), args);
            *stack = CAPSULE_CAR(*stack);
            return CAPSULE_ERROR_NONE;
        } else {
            goto store_arg;
        }
    } else if (op.type == CAPSULE_TYPE_MACRO) {

        *expr = *result;
        *stack = CAPSULE_CAR(*stack);
        return CAPSULE_ERROR_NONE;
    } else {
    store_arg:

        args = Capsule_List_at(*stack, 4);
        Capsule_List_set(*stack, 4, CAPSULE_CONS(*result, args));
    }

    args = Capsule_List_at(*stack, 3);
    if (CAPSULE_NILP(args)) { return eval_do_apply(stack, expr, env, result); }

    *expr = CAPSULE_CAR(args);
    Capsule_List_set(*stack, 3, CAPSULE_CDR(args));
    return CAPSULE_ERROR_NONE;
}

CapsuleError Capsule_eval_cap(Capsule expr, Capsule scope, Capsule* result) {
    static int count = 0;
    CapsuleError error = CAPSULE_ERROR_NONE;
    Capsule stack = Capsule_nil;

    do {
        if (++count >= GC_THRESHOLD) {
            gc_mark(expr);
            gc_mark(scope);
            gc_mark(stack);
            gc();
            count = 0;
        }

        if (expr.type == CAPSULE_TYPE_SYMBOL) {
            error = Capsule_Scope_lookup(scope, expr, result);
            if (error == CAPSULE_ERROR_UNBOUND) { fprintf(stderr, "Unbound symbol %s\n", expr.as.symbol); }
        } else if (expr.type != CAPSULE_TYPE_PAIR) {
            *result = expr;
        } else if (!CAPSULE_LISTP(expr)) {
            return CAPSULE_ERROR_SYNTAX;
        } else {
            Capsule op = CAPSULE_CAR(expr);
            Capsule args = CAPSULE_CDR(expr);

            if (op.type == CAPSULE_TYPE_SYMBOL) {

                if (strcmp(op.as.symbol, "QUOTE") == 0) {
                    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args))) return CAPSULE_ERROR_ARGS;

                    *result = CAPSULE_CAR(args);
                } else if (strcmp(op.as.symbol, "DEFINE") == 0) {
                    Capsule sym;

                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args))) return CAPSULE_ERROR_ARGS;

                    sym = CAPSULE_CAR(args);
                    if (sym.type == CAPSULE_TYPE_PAIR) {
                        error = make_closure(scope, CAPSULE_CDR(sym), CAPSULE_CDR(args), result);
                        sym = CAPSULE_CAR(sym);
                        if (sym.type != CAPSULE_TYPE_SYMBOL) return CAPSULE_ERROR_TYPE;
                        (void)Capsule_Scope_define(scope, sym, *result);
                        *result = sym;
                    } else if (sym.type == CAPSULE_TYPE_SYMBOL) {
                        if (!CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args)))) return CAPSULE_ERROR_ARGS;
                        stack = make_frame(stack, scope, Capsule_nil);
                        Capsule_List_set(stack, 2, op);
                        Capsule_List_set(stack, 4, sym);
                        expr = CAPSULE_CAR(CAPSULE_CDR(args));
                        continue;
                    } else {
                        return CAPSULE_ERROR_TYPE;
                    }
                } else if (strcmp(op.as.symbol, "LAMBDA") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args))) return CAPSULE_ERROR_ARGS;

                    error = make_closure(scope, CAPSULE_CAR(args), CAPSULE_CDR(args), result);
                } else if (CAPSULE_SYMBOL_COMPARE(op, "BEGIN")) {
                    if (!CAPSULE_NILP(args)) {
                        stack = make_frame(stack, scope, CAPSULE_CDR(args));
                        Capsule_List_set(stack, 2, op);
                        expr = CAPSULE_CAR(args);
                        continue;
                    }
                } else if (strcmp(op.as.symbol, "IF") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) ||
                            CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))) ||
                            !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(CAPSULE_CDR(args)))))
                        return CAPSULE_ERROR_ARGS;

                    stack = make_frame(stack, scope, CAPSULE_CDR(args));
                    Capsule_List_set(stack, 2, op);
                    expr = CAPSULE_CAR(args);
                    continue;
                } else if (strcmp(op.as.symbol, "DEFMACRO") == 0) {
                    Capsule name, macro;

                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args))) return CAPSULE_ERROR_ARGS;

                    if (CAPSULE_CAR(args).type != CAPSULE_TYPE_PAIR) return CAPSULE_ERROR_SYNTAX;

                    name = CAPSULE_CAR(CAPSULE_CAR(args));
                    if (name.type != CAPSULE_TYPE_SYMBOL) return CAPSULE_ERROR_TYPE;

                    error = make_closure(scope, CAPSULE_CDR(CAPSULE_CAR(args)), CAPSULE_CDR(args), &macro);
                    if (!error) {
                        macro.type = CAPSULE_TYPE_MACRO;
                        *result = name;
                        (void)Capsule_Scope_define(scope, name, macro);
                    }
                } else if (strcmp(op.as.symbol, "APPLY") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) ||
                            !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
                        return CAPSULE_ERROR_ARGS;

                    stack = make_frame(stack, scope, CAPSULE_CDR(args));
                    Capsule_List_set(stack, 2, op);
                    expr = CAPSULE_CAR(args);
                    continue;
                } else if (strcmp(op.as.symbol, "SET!") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) ||
                            !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
                        return CAPSULE_ERROR_ARGS;
                    if (CAPSULE_CAR(args).type != CAPSULE_TYPE_SYMBOL) return CAPSULE_ERROR_TYPE;
                    stack = make_frame(stack, scope, Capsule_nil);
                    Capsule_List_set(stack, 2, op);
                    Capsule_List_set(stack, 4, CAPSULE_CAR(args));
                    expr = CAPSULE_CAR(CAPSULE_CDR(args));
                    continue;
                } else {
                    goto push;
                }
            } else if (op.type == CAPSULE_TYPE_BUILTIN) {
                if ((error = (*op.as.builtin)(args, scope, result))) {}
            } else {
            push:

                stack = make_frame(stack, scope, args);
                expr = op;
                continue;
            }
        }

        if (CAPSULE_NILP(stack)) break;

        if (!error) error = eval_do_return(&stack, &expr, &scope, result);
    } while (!error);

    return error;
}

CapsuleError Capsule_eval(const char* source, Capsule scope, Capsule* result) {
    CapsuleError error;
    Capsule capsule;
    if ((error = Capsule_read(source, &capsule))) return error;
    return Capsule_eval_cap(capsule, scope, result);
}
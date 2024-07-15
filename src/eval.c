#include <string.h>

#include "_priv.h"

Capsule make_closure(Capsule scope, Capsule args, Capsule body) {
    Capsule p;

    if (!Capsule_listp(body))
        return CAPSULE_ERROR(CapsuleError_Syntax);

    p = args;
    while (!CAPSULE_NILP(p)) {
        if (p.type == CapsuleType_Symbol)
            break;
        else if (p.type != CapsuleType_Pair || CAPSULE_CAR(p).type != CapsuleType_Symbol)
            return CAPSULE_ERROR(CapsuleError_InvalidType);
        p = CAPSULE_CDR(p);
    }

    Capsule result = Capsule_cons(scope, Capsule_cons(args, body));
    result.type = CapsuleType_Closure;

    return result;
}

Capsule make_frame(Capsule parent, Capsule scope, Capsule tail) {
    return Capsule_cons(parent,
                        Capsule_cons(scope,
                                     Capsule_cons(Capsule_nil,
                                                  Capsule_cons(tail,
                                                               Capsule_cons(Capsule_nil,
                                                                            Capsule_cons(Capsule_nil,
                                                                                         Capsule_nil))))));
}

int eval_do_exec(Capsule *stack, Capsule *expr, Capsule *scope) {
    Capsule body;

    *scope = Capsule_list_at(*stack, 1);
    body = Capsule_list_at(*stack, 5);
    *expr = CAPSULE_CAR(body);
    body = CAPSULE_CDR(body);
    if (CAPSULE_NILP(body)) {
        *stack = CAPSULE_CAR(*stack);
    } else {
        Capsule_list_set(*stack, 5, body);
    }

    return CapsuleError_None;
}

int eval_do_bind(Capsule *stack, Capsule *expr, Capsule *scope) {
    Capsule op, args, arg_names, body;

    body = Capsule_list_at(*stack, 5);
    if (!CAPSULE_NILP(body))
        return eval_do_exec(stack, expr, scope);

    op = Capsule_list_at(*stack, 2);
    args = Capsule_list_at(*stack, 4);

    *scope = Capsule_scope_new(CAPSULE_CAR(op));
    arg_names = CAPSULE_CAR(CAPSULE_CDR(op));
    body = CAPSULE_CDR(CAPSULE_CDR(op));
    Capsule_list_set(*stack, 1, *scope);
    Capsule_list_set(*stack, 5, body);

    while (!CAPSULE_NILP(arg_names)) {
        if (arg_names.type == CapsuleType_Symbol) {
            Capsule_scope_define(*scope, arg_names, args);
            args = Capsule_nil;
            break;
        }

        if (CAPSULE_NILP(args))
            return CapsuleError_InvalidArgs;
        Capsule_scope_define(*scope, CAPSULE_CAR(arg_names), CAPSULE_CAR(args));
        arg_names = CAPSULE_CDR(arg_names);
        args = CAPSULE_CDR(args);
    }
    if (!CAPSULE_NILP(args))
        return CapsuleError_InvalidArgs;

    Capsule_list_set(*stack, 4, Capsule_nil);

    return eval_do_exec(stack, expr, scope);
}

int eval_do_apply(Capsule *stack, Capsule *expr, Capsule *scope, Capsule *result) {
    Capsule op, args;

    op = Capsule_list_at(*stack, 2);
    args = Capsule_list_at(*stack, 4);

    if (!CAPSULE_NILP(args)) {
        Capsule_list_reverse(&args);
        Capsule_list_set(*stack, 4, args);
    }

    if (op.type == CapsuleType_Symbol) {
        if (strcmp(op.as.symbol, "APPLY") == 0) {
            *stack = CAPSULE_CAR(*stack);
            *stack = make_frame(*stack, *scope, Capsule_nil);
            op = CAPSULE_CAR(args);
            args = CAPSULE_CAR(CAPSULE_CDR(args));
            if (!Capsule_listp(args))
                return CapsuleError_Syntax;

            Capsule_list_set(*stack, 2, op);
            Capsule_list_set(*stack, 4, args);
        }
    }

    if (op.type == CapsuleType_Builtin) {
        *stack = CAPSULE_CAR(*stack);
        *expr = Capsule_cons(op, args);
        return CapsuleError_None;
    } else if (op.type != CapsuleType_Closure) {
        return CapsuleError_InvalidType;
    }

    return eval_do_bind(stack, expr, scope);
}

int eval_do_return(Capsule *stack, Capsule *expr, Capsule *scope, Capsule *result) {
    Capsule op, args, body;

    *scope = Capsule_list_at(*stack, 1);
    op = Capsule_list_at(*stack, 2);
    body = Capsule_list_at(*stack, 5);

    if (!CAPSULE_NILP(body)) {
        return eval_do_apply(stack, expr, scope, result);
    }

    if (CAPSULE_NILP(op)) {
        op = *result;
        Capsule_list_set(*stack, 2, op);

        if (op.type == CapsuleType_Macro) {
            args = Capsule_list_at(*stack, 3);
            *stack = make_frame(*stack, *scope, Capsule_nil);
            op.type = CapsuleType_Closure;
            Capsule_list_set(*stack, 2, op);
            Capsule_list_set(*stack, 4, args);
            return eval_do_bind(stack, expr, scope);
        }
    } else if (op.type == CapsuleType_Symbol) {
        if (strcmp(op.as.symbol, "DEFINE") == 0) {
            Capsule sym = Capsule_list_at(*stack, 4);
            (void)Capsule_scope_define(*scope, sym, *result);
            *stack = CAPSULE_CAR(*stack);
            *expr = Capsule_cons(CAPSULE_SYMBOL("QUOTE"), Capsule_cons(sym, Capsule_nil));
            return CapsuleError_None;
        } else if (strcmp(op.as.symbol, "BEGIN") == 0) {
            args = Capsule_list_at(*stack, 3);
            *expr = CAPSULE_CONS(CAPSULE_SYMBOL("BEGIN"), args);
            *stack = CAPSULE_CAR(*stack);
            return CapsuleError_None;
        } else if (strcmp(op.as.symbol, "SET!") == 0) {
            Capsule sym = Capsule_list_at(*stack, 4);
            *stack = CAPSULE_CAR(*stack);
            *expr = Capsule_cons(CAPSULE_SYMBOL("QUOTE"), Capsule_cons(sym, Capsule_nil));
            return Capsule_scope_set(*scope, sym, *result);
        } else if (strcmp(op.as.symbol, "IF") == 0) {
            args = Capsule_list_at(*stack, 3);
            *expr = CAPSULE_NILP(*result) ? CAPSULE_CAR(CAPSULE_CDR(args)) : CAPSULE_CAR(args);
            *stack = CAPSULE_CAR(*stack);
            return CapsuleError_None;
        } else {
            goto store_arg;
        }
    } else if (op.type == CapsuleType_Macro) {
        *expr = *result;
        *stack = CAPSULE_CAR(*stack);
        return CapsuleError_None;
    } else {
    store_arg:

        args = Capsule_list_at(*stack, 4);
        Capsule_list_set(*stack, 4, Capsule_cons(*result, args));
    }

    args = Capsule_list_at(*stack, 3);
    if (CAPSULE_NILP(args)) {
        return eval_do_apply(stack, expr, scope, result);
    }

    *expr = CAPSULE_CAR(args);
    Capsule_list_set(*stack, 3, CAPSULE_CDR(args));
    return CapsuleError_None;
}

Capsule Capsule_eval_expr(Capsule expr, Capsule scope) {
    static int count = 0;
    Capsule stack = Capsule_nil;
    Capsule result = Capsule_nil;
    do {
        if (++count == 100000) {
            gc_mark(expr);
            gc_mark(scope);
            gc_mark(stack);
            gc();
            count = 0;
        }

        if (expr.type == CapsuleType_Symbol) {
            result = Capsule_scope_lookup(scope, expr);
        } else if (expr.type != CapsuleType_Pair) {
            result = expr;
        } else if (!Capsule_listp(expr)) {
            return CAPSULE_ERROR(CapsuleError_Syntax);
        } else {
            Capsule op = CAPSULE_CAR(expr);
            Capsule args = CAPSULE_CDR(expr);

            if (op.type == CapsuleType_Symbol) {
                if (strcmp(op.as.symbol, "QUOTE") == 0) {
                    if (CAPSULE_NILP(args) || !CAPSULE_NILP(CAPSULE_CDR(args)))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    result = CAPSULE_CAR(args);
                } else if (strcmp(op.as.symbol, "DEFINE") == 0) {
                    Capsule sym;

                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    sym = CAPSULE_CAR(args);
                    if (sym.type == CapsuleType_Pair) {
                        result = make_closure(scope, CAPSULE_CDR(sym), CAPSULE_CDR(args));
                        sym = CAPSULE_CAR(sym);
                        if (sym.type != CapsuleType_Symbol)
                            return CAPSULE_ERROR(CapsuleError_InvalidType);
                        (void)Capsule_scope_define(scope, sym, result);
                        result = sym;
                    } else if (sym.type == CapsuleType_Symbol) {
                        if (!CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
                            return CAPSULE_ERROR(CapsuleError_InvalidArgs);
                        stack = make_frame(stack, scope, Capsule_nil);
                        Capsule_list_set(stack, 2, op);
                        Capsule_list_set(stack, 4, sym);
                        expr = CAPSULE_CAR(CAPSULE_CDR(args));
                        continue;
                    } else {
                        return CAPSULE_ERROR(CapsuleError_InvalidType);
                    }
                } else if (strcmp(op.as.symbol, "LAMBDA") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    result = make_closure(scope, CAPSULE_CAR(args), CAPSULE_CDR(args));
                } else if (strcmp(op.as.symbol, "BEGIN") == 0) {
                    if (CAPSULE_NILP(args))
                        break;
                    stack = make_frame(stack, scope, CAPSULE_CDR(args));
                    Capsule_list_set(stack, 2, op);
                    expr = CAPSULE_CAR(args);
                    continue;
                } else if (strcmp(op.as.symbol, "IF") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(CAPSULE_CDR(args)))))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    stack = make_frame(stack, scope, CAPSULE_CDR(args));
                    Capsule_list_set(stack, 2, op);
                    expr = CAPSULE_CAR(args);
                    continue;
                } else if (strcmp(op.as.symbol, "DEFMACRO") == 0) {
                    Capsule name, macro;

                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    if (CAPSULE_CAR(args).type != CapsuleType_Pair)
                        return CAPSULE_ERROR(CapsuleError_Syntax);

                    name = CAPSULE_CAR(CAPSULE_CAR(args));
                    if (name.type != CapsuleType_Symbol)
                        return CAPSULE_ERROR(CapsuleError_InvalidType);

                    macro = make_closure(scope, CAPSULE_CDR(CAPSULE_CAR(args)),
                                         CAPSULE_CDR(args));
                    if (!CAPSULE_ERRORP(macro)) {
                        macro.type = CapsuleType_Macro;
                        result = name;
                        (void)Capsule_scope_define(scope, name, macro);
                    }
                } else if (strcmp(op.as.symbol, "APPLY") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);

                    stack = make_frame(stack, scope, CAPSULE_CDR(args));
                    Capsule_list_set(stack, 2, op);
                    expr = CAPSULE_CAR(args);
                    continue;
                } else if (strcmp(op.as.symbol, "SET!") == 0) {
                    if (CAPSULE_NILP(args) || CAPSULE_NILP(CAPSULE_CDR(args)) || !CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(args))))
                        return CAPSULE_ERROR(CapsuleError_InvalidArgs);
                    if (CAPSULE_CAR(args).type != CapsuleType_Symbol)
                        return CAPSULE_ERROR(CapsuleError_InvalidType);
                    stack = make_frame(stack, scope, Capsule_nil);
                    Capsule_list_set(stack, 2, op);
                    Capsule_list_set(stack, 4, CAPSULE_CAR(args));
                    expr = CAPSULE_CAR(CAPSULE_CDR(args));
                    continue;
                } else {
                    goto push;
                }
            } else if (op.type == CapsuleType_Builtin) {
                result = (*op.as.builtin)(args, scope);
            } else {
            push:

                stack = make_frame(stack, scope, args);
                expr = op;
                continue;
            }
        }

        if (CAPSULE_NILP(stack))
            break;

        if (!CAPSULE_ERRORP(result)) {
            CapsuleError error = eval_do_return(&stack, &expr, &scope, &result);
            if (error != CapsuleError_None) return CAPSULE_ERROR(error);
        }
    } while (!CAPSULE_ERRORP(result));

    return result;
}

Capsule Capsule_eval(const char *source, Capsule scope) {
    Capsule expr = Capsule_read(source);
    if (CAPSULE_ERRORP(expr)) return expr;
    return Capsule_eval_expr(expr, scope);
}
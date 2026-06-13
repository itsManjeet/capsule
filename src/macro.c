#include "lipi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_MAX_DEPTH 1024

typedef enum {
    MVAL_AST,
    MVAL_LIST,
    MVAL_NULL
} MValKind;

typedef struct {
    MValKind kind;
    LipiAst *ast;
    LipiVec list; /* LipiAst* */
} MVal;

typedef struct {
    LipiCompiler *c;
    LipiMacro *macro;
    LipiVec names;  /* char* */
    LipiVec values; /* MVal* */
    LipiSpan call_span;
} MEnv;

static MVal *mval_new(LipiCompiler *c, MValKind kind) {
    MVal *v = (MVal *)lipi_arena_alloc(&c->arena, sizeof(MVal));
    v->kind = kind;
    return v;
}

static MVal *mval_ast(LipiCompiler *c, LipiAst *ast) {
    MVal *v = mval_new(c, MVAL_AST);
    v->ast = ast;
    return v;
}

static MVal *mval_list(LipiCompiler *c) {
    return mval_new(c, MVAL_LIST);
}

static MVal *env_get(MEnv *env, const char *name) {
    for (int i = 0; i < env->names.len; i++) {
        if (strcmp((char *)env->names.items[i], name) == 0) {
            return (MVal *)env->values.items[i];
        }
    }
    return NULL;
}

static LipiMacro *find_macro(LipiCompiler *c, const char *name) {
    for (int i = 0; i < c->macros.len; i++) {
        LipiMacro *m = (LipiMacro *)c->macros.items[i];
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }
    return NULL;
}

static LipiAst *mval_to_ast(LipiCompiler *c, MVal *v, LipiSpan span) {
    if (!v || v->kind == MVAL_NULL) {
        LipiAst *list = lipi_ast_new(c, AST_LIST, span);
        return list;
    }
    if (v->kind == MVAL_AST) {
        return lipi_ast_clone(c, v->ast);
    }
    LipiAst *list = lipi_ast_new(c, AST_LIST, span);
    for (int i = 0; i < v->list.len; i++) {
        lipi_vec_push(&list->as.items, lipi_ast_clone(c, (LipiAst *)v->list.items[i]));
    }
    return list;
}

static int value_is_list(MVal *v) {
    return v && (v->kind == MVAL_LIST || (v->kind == MVAL_AST && v->ast && v->ast->kind == AST_LIST));
}

static LipiVec *value_items(MVal *v) {
    if (v->kind == MVAL_LIST) {
        return &v->list;
    }
    if (v->kind == MVAL_AST && v->ast && v->ast->kind == AST_LIST) {
        return &v->ast->as.items;
    }
    return NULL;
}

static void macro_error(MEnv *env, LipiSpan span, const char *message, const char *detail) {
    char buf[512];
    snprintf(buf, sizeof(buf), "macro expansion failed in '%s'", env->macro->name);
    lipi_diag_error(env->c, span.file ? span : env->call_span, buf, detail ? detail : message);
}

static MVal *eval_macro(MEnv *env, LipiAst *node);

static MVal *qq(MEnv *env, LipiAst *node) {
    if (node->kind == AST_LIST && node->as.items.len >= 1 &&
        lipi_ast_is_symbol((LipiAst *)node->as.items.items[0], "unquote")) {
        if (node->as.items.len != 2) {
            macro_error(env, node->span, "invalid unquote", "unquote expects one expression");
            return NULL;
        }
        return eval_macro(env, (LipiAst *)node->as.items.items[1]);
    }

    if (node->kind == AST_LIST) {
        LipiAst *list = lipi_ast_new(env->c, AST_LIST, node->span);
        for (int i = 0; i < node->as.items.len; i++) {
            LipiAst *item = (LipiAst *)node->as.items.items[i];
            if (item->kind == AST_LIST && item->as.items.len >= 1 &&
                lipi_ast_is_symbol((LipiAst *)item->as.items.items[0], "unquote-splicing")) {
                if (item->as.items.len != 2) {
                    macro_error(env, item->span, "invalid unquote-splicing",
                                "unquote-splicing expects one expression");
                    return NULL;
                }
                MVal *spliced = eval_macro(env, (LipiAst *)item->as.items.items[1]);
                if (!spliced || !value_is_list(spliced)) {
                    macro_error(env, item->span, "invalid unquote-splicing",
                                "unquote-splicing expected a list");
                    return NULL;
                }
                LipiVec *items = value_items(spliced);
                for (int j = 0; j < items->len; j++) {
                    lipi_vec_push(&list->as.items, lipi_ast_clone(env->c, (LipiAst *)items->items[j]));
                }
                continue;
            }
            MVal *q = qq(env, item);
            if (!q) {
                return NULL;
            }
            lipi_vec_push(&list->as.items, mval_to_ast(env->c, q, item->span));
        }
        return mval_ast(env->c, list);
    }

    if (node->kind == AST_ARRAY || node->kind == AST_MAP) {
        LipiAst *copy = lipi_ast_new(env->c, node->kind, node->span);
        for (int i = 0; i < node->as.items.len; i++) {
            MVal *q = qq(env, (LipiAst *)node->as.items.items[i]);
            if (!q) {
                return NULL;
            }
            lipi_vec_push(&copy->as.items, mval_to_ast(env->c, q, ((LipiAst *)node->as.items.items[i])->span));
        }
        return mval_ast(env->c, copy);
    }

    return mval_ast(env->c, lipi_ast_clone(env->c, node));
}

static LipiAst *bool_ast(LipiCompiler *c, int value, LipiSpan span) {
    LipiAst *node = lipi_ast_new(c, AST_BOOL, span);
    node->as.bool_value = value;
    return node;
}

static LipiAst *int_ast(LipiCompiler *c, int64_t value, LipiSpan span) {
    LipiAst *node = lipi_ast_new(c, AST_INT, span);
    node->as.int_value = value;
    return node;
}

static int mval_truthy(MVal *v) {
    if (!v || v->kind == MVAL_NULL) {
        return 0;
    }
    if (v->kind == MVAL_LIST) {
        return v->list.len != 0;
    }
    if (!v->ast) {
        return 0;
    }
    if (v->ast->kind == AST_BOOL) {
        return v->ast->as.bool_value;
    }
    if (v->ast->kind == AST_LIST) {
        return v->ast->as.items.len != 0;
    }
    return 1;
}

static MVal *builtin_list(MEnv *env, LipiAst *node) {
    LipiAst *list = lipi_ast_new(env->c, AST_LIST, node->span);
    for (int i = 1; i < node->as.items.len; i++) {
        MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[i]);
        if (!v) return NULL;
        lipi_vec_push(&list->as.items, mval_to_ast(env->c, v, ((LipiAst *)node->as.items.items[i])->span));
    }
    return mval_ast(env->c, list);
}

static MVal *builtin_append(MEnv *env, LipiAst *node) {
    LipiAst *list = lipi_ast_new(env->c, AST_LIST, node->span);
    for (int i = 1; i < node->as.items.len; i++) {
        MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[i]);
        if (!v || !value_is_list(v)) {
            macro_error(env, ((LipiAst *)node->as.items.items[i])->span, "append expected a list", NULL);
            return NULL;
        }
        LipiVec *items = value_items(v);
        for (int j = 0; j < items->len; j++) {
            lipi_vec_push(&list->as.items, lipi_ast_clone(env->c, (LipiAst *)items->items[j]));
        }
    }
    return mval_ast(env->c, list);
}

static MVal *builtin_cons(MEnv *env, LipiAst *node) {
    if (node->as.items.len != 3) {
        macro_error(env, node->span, "cons expects 2 arguments", NULL);
        return NULL;
    }
    MVal *head = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    MVal *tail = eval_macro(env, (LipiAst *)node->as.items.items[2]);
    if (!head || !tail || !value_is_list(tail)) {
        macro_error(env, node->span, "cons expected a value and a list", NULL);
        return NULL;
    }
    LipiAst *list = lipi_ast_new(env->c, AST_LIST, node->span);
    lipi_vec_push(&list->as.items, mval_to_ast(env->c, head, node->span));
    LipiVec *items = value_items(tail);
    for (int i = 0; i < items->len; i++) {
        lipi_vec_push(&list->as.items, lipi_ast_clone(env->c, (LipiAst *)items->items[i]));
    }
    return mval_ast(env->c, list);
}

static MVal *builtin_car_cdr(MEnv *env, LipiAst *node, int car) {
    if (node->as.items.len != 2) {
        macro_error(env, node->span, car ? "car expects 1 argument" : "cdr expects 1 argument", NULL);
        return NULL;
    }
    MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    if (!v || !value_is_list(v)) {
        macro_error(env, node->span, car ? "car expected a list" : "cdr expected a list", NULL);
        return NULL;
    }
    LipiVec *items = value_items(v);
    if (car) {
        if (!items->len) {
            return mval_new(env->c, MVAL_NULL);
        }
        return mval_ast(env->c, lipi_ast_clone(env->c, (LipiAst *)items->items[0]));
    }
    MVal *out = mval_list(env->c);
    for (int i = 1; i < items->len; i++) {
        lipi_vec_push(&out->list, lipi_ast_clone(env->c, (LipiAst *)items->items[i]));
    }
    return out;
}

static MVal *builtin_pred(MEnv *env, LipiAst *node, const char *name) {
    if (node->as.items.len != 2) {
        macro_error(env, node->span, "predicate expects 1 argument", NULL);
        return NULL;
    }
    MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    if (!v) return NULL;
    int result = 0;
    if (strcmp(name, "null?") == 0) {
        result = (v->kind == MVAL_NULL) || (value_is_list(v) && value_items(v)->len == 0);
    } else if (strcmp(name, "list?") == 0) {
        result = value_is_list(v);
    } else if (strcmp(name, "symbol?") == 0) {
        result = v->kind == MVAL_AST && v->ast && v->ast->kind == AST_SYMBOL;
    }
    return mval_ast(env->c, bool_ast(env->c, result, node->span));
}

static MVal *builtin_gensym(MEnv *env, LipiAst *node) {
    static int gensym_id = 0;
    if (node->as.items.len > 2) {
        macro_error(env, node->span, "gensym expects 0 or 1 arguments", NULL);
        return NULL;
    }
    const char *prefix = "g";
    if (node->as.items.len == 2) {
        MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[1]);
        if (!v || v->kind != MVAL_AST || !v->ast || v->ast->kind != AST_STRING) {
            macro_error(env, node->span, "gensym prefix must be a string", NULL);
            return NULL;
        }
        prefix = v->ast->as.text;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s__%d", prefix, ++gensym_id);
    return mval_ast(env->c, lipi_ast_symbol(env->c, buf, node->span));
}

static MVal *builtin_length(MEnv *env, LipiAst *node) {
    if (node->as.items.len != 2) {
        macro_error(env, node->span, "length expects 1 argument", NULL);
        return NULL;
    }
    MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    if (!v || !value_is_list(v)) {
        macro_error(env, node->span, "length expected a list", NULL);
        return NULL;
    }
    return mval_ast(env->c, int_ast(env->c, value_items(v)->len, node->span));
}

static MVal *builtin_eq(MEnv *env, LipiAst *node) {
    if (node->as.items.len != 3) {
        macro_error(env, node->span, "= expects 2 arguments", NULL);
        return NULL;
    }
    MVal *a = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    MVal *b = eval_macro(env, (LipiAst *)node->as.items.items[2]);
    if (!a || !b || a->kind != MVAL_AST || b->kind != MVAL_AST || !a->ast || !b->ast) {
        macro_error(env, node->span, "= expected comparable syntax values", NULL);
        return NULL;
    }
    int equal = 0;
    if (a->ast->kind == AST_INT && b->ast->kind == AST_INT) {
        equal = a->ast->as.int_value == b->ast->as.int_value;
    } else if (a->ast->kind == AST_BOOL && b->ast->kind == AST_BOOL) {
        equal = a->ast->as.bool_value == b->ast->as.bool_value;
    } else if (a->ast->kind == AST_SYMBOL && b->ast->kind == AST_SYMBOL) {
        equal = strcmp(a->ast->as.text, b->ast->as.text) == 0;
    } else if (a->ast->kind == AST_STRING && b->ast->kind == AST_STRING) {
        equal = strcmp(a->ast->as.text, b->ast->as.text) == 0;
    }
    return mval_ast(env->c, bool_ast(env->c, equal, node->span));
}

static MVal *builtin_if(MEnv *env, LipiAst *node) {
    if (node->as.items.len != 4) {
        macro_error(env, node->span, "if expects condition, then, and else expressions", NULL);
        return NULL;
    }
    MVal *cond = eval_macro(env, (LipiAst *)node->as.items.items[1]);
    if (!cond) return NULL;
    return eval_macro(env, (LipiAst *)node->as.items.items[mval_truthy(cond) ? 2 : 3]);
}

static MVal *builtin_begin(MEnv *env, LipiAst *node) {
    MVal *result = mval_new(env->c, MVAL_NULL);
    for (int i = 1; i < node->as.items.len; i++) {
        result = eval_macro(env, (LipiAst *)node->as.items.items[i]);
        if (!result) return NULL;
    }
    return result;
}

static MVal *builtin_macro_error(MEnv *env, LipiAst *node) {
    const char *detail = "macro-error";
    if (node->as.items.len == 2) {
        MVal *v = eval_macro(env, (LipiAst *)node->as.items.items[1]);
        if (v && v->kind == MVAL_AST && v->ast && v->ast->kind == AST_STRING) {
            detail = v->ast->as.text;
        }
    }
    macro_error(env, node->span, detail, detail);
    return NULL;
}

static MVal *eval_macro(MEnv *env, LipiAst *node) {
    if (node->kind == AST_SYMBOL) {
        MVal *bound = env_get(env, node->as.text);
        if (bound) {
            return bound;
        }
        return mval_ast(env->c, lipi_ast_clone(env->c, node));
    }
    if (node->kind != AST_LIST || node->as.items.len == 0) {
        return mval_ast(env->c, lipi_ast_clone(env->c, node));
    }

    LipiAst *head = (LipiAst *)node->as.items.items[0];
    if (head->kind != AST_SYMBOL) {
        return mval_ast(env->c, lipi_ast_clone(env->c, node));
    }
    const char *name = head->as.text;
    if (strcmp(name, "quote") == 0) {
        if (node->as.items.len != 2) {
            macro_error(env, node->span, "quote expects one expression", NULL);
            return NULL;
        }
        return mval_ast(env->c, lipi_ast_clone(env->c, (LipiAst *)node->as.items.items[1]));
    }
    if (strcmp(name, "quasiquote") == 0) {
        if (node->as.items.len != 2) {
            macro_error(env, node->span, "quasiquote expects one expression", NULL);
            return NULL;
        }
        return qq(env, (LipiAst *)node->as.items.items[1]);
    }
    if (strcmp(name, "if") == 0) return builtin_if(env, node);
    if (strcmp(name, "begin") == 0) return builtin_begin(env, node);
    if (strcmp(name, "=") == 0) return builtin_eq(env, node);
    if (strcmp(name, "length") == 0) return builtin_length(env, node);
    if (strcmp(name, "list") == 0) return builtin_list(env, node);
    if (strcmp(name, "append") == 0) return builtin_append(env, node);
    if (strcmp(name, "cons") == 0) return builtin_cons(env, node);
    if (strcmp(name, "car") == 0) return builtin_car_cdr(env, node, 1);
    if (strcmp(name, "cdr") == 0) return builtin_car_cdr(env, node, 0);
    if (strcmp(name, "null?") == 0 || strcmp(name, "list?") == 0 || strcmp(name, "symbol?") == 0) {
        return builtin_pred(env, node, name);
    }
    if (strcmp(name, "gensym") == 0) return builtin_gensym(env, node);
    if (strcmp(name, "macro-error") == 0) return builtin_macro_error(env, node);
    if (strcmp(name, "unquote") == 0 || strcmp(name, "unquote-splicing") == 0) {
        macro_error(env, node->span, "unquote outside quasiquote", NULL);
        return NULL;
    }
    return mval_ast(env->c, lipi_ast_clone(env->c, node));
}

static int parse_macro_def(LipiCompiler *c, LipiAst *form, LipiMacro **out) {
    if (!form || form->kind != AST_LIST || form->as.items.len < 3 ||
        !lipi_ast_is_symbol((LipiAst *)form->as.items.items[0], "define-macro")) {
        return 0;
    }
    LipiAst *header = (LipiAst *)form->as.items.items[1];
    if (header->kind != AST_LIST || header->as.items.len < 1 ||
        ((LipiAst *)header->as.items.items[0])->kind != AST_SYMBOL) {
        lipi_diag_error(c, form->span, "invalid define-macro form",
                        "expected (define-macro (name args...) body...)");
        return -1;
    }
    LipiMacro *m = (LipiMacro *)lipi_arena_alloc(&c->arena, sizeof(LipiMacro));
    m->name = ((LipiAst *)header->as.items.items[0])->as.text;
    m->span = ((LipiAst *)header->as.items.items[0])->span;
    for (int i = 1; i < header->as.items.len; i++) {
        LipiAst *param = (LipiAst *)header->as.items.items[i];
        if (param->kind != AST_SYMBOL) {
            lipi_diag_error(c, param->span, "invalid macro parameter", "expected symbol");
            return -1;
        }
        char *pname = param->as.text;
        if (lipi_ends_with(pname, "...")) {
            if (i != header->as.items.len - 1) {
                lipi_diag_error(c, param->span, "invalid variadic macro parameter",
                                "variadic parameter must be last");
                return -1;
            }
            m->variadic = 1;
            m->variadic_name = lipi_arena_strndup(&c->arena, pname, strlen(pname) - 3);
        } else {
            lipi_vec_push(&m->params, pname);
        }
    }
    for (int i = 2; i < form->as.items.len; i++) {
        lipi_vec_push(&m->body, form->as.items.items[i]);
    }
    *out = m;
    return 1;
}

static int bind_macro_args(LipiCompiler *c, LipiMacro *m, LipiAst *call, MEnv *env) {
    int argc = call->as.items.len - 1;
    int fixed = m->params.len;
    if ((!m->variadic && argc != fixed) || (m->variadic && argc < fixed)) {
        char detail[128];
        if (m->variadic) {
            snprintf(detail, sizeof(detail), "macro expects at least %d arguments but got %d", fixed, argc);
        } else {
            snprintf(detail, sizeof(detail), "macro expects %d arguments but got %d", fixed, argc);
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "macro expansion failed in '%s'", m->name);
        lipi_diag_error(c, call->span, msg, detail);
        return 0;
    }
    for (int i = 0; i < fixed; i++) {
        lipi_vec_push(&env->names, m->params.items[i]);
        lipi_vec_push(&env->values, mval_ast(c, lipi_ast_clone(c, (LipiAst *)call->as.items.items[i + 1])));
    }
    if (m->variadic) {
        MVal *rest = mval_list(c);
        for (int i = fixed; i < argc; i++) {
            lipi_vec_push(&rest->list, lipi_ast_clone(c, (LipiAst *)call->as.items.items[i + 1]));
        }
        lipi_vec_push(&env->names, m->variadic_name);
        lipi_vec_push(&env->values, rest);
    }
    return 1;
}

static LipiAst *expand_ast(LipiCompiler *c, LipiAst *node, int depth);

static LipiAst *apply_macro(LipiCompiler *c, LipiMacro *m, LipiAst *call, int depth) {
    if (depth > MACRO_MAX_DEPTH) {
        char msg[256];
        snprintf(msg, sizeof(msg), "macro expansion exceeded maximum depth while expanding '%s'", m->name);
        lipi_diag_error(c, call->span, msg, NULL);
        return NULL;
    }
    MEnv env;
    memset(&env, 0, sizeof(env));
    env.c = c;
    env.macro = m;
    env.call_span = call->span;
    if (!bind_macro_args(c, m, call, &env)) {
        return NULL;
    }
    MVal *result = NULL;
    for (int i = 0; i < m->body.len; i++) {
        result = eval_macro(&env, (LipiAst *)m->body.items[i]);
        if (!result) {
            return NULL;
        }
    }
    LipiAst *replacement = mval_to_ast(c, result, call->span);
    return expand_ast(c, replacement, depth + 1);
}

static LipiAst *expand_children_generic(LipiCompiler *c, LipiAst *node, int depth) {
    if (node->kind == AST_LIST || node->kind == AST_ARRAY || node->kind == AST_MAP) {
        for (int i = 0; i < node->as.items.len; i++) {
            LipiAst *item = expand_ast(c, (LipiAst *)node->as.items.items[i], depth);
            if (!item) return NULL;
            node->as.items.items[i] = item;
        }
    }
    return node;
}

static LipiAst *expand_define(LipiCompiler *c, LipiAst *node, int depth) {
    if (node->as.items.len < 3) {
        return node;
    }
    LipiAst *target = (LipiAst *)node->as.items.items[1];
    if (target->kind == AST_LIST) {
        for (int i = 2; i < node->as.items.len; i++) {
            LipiAst *item = expand_ast(c, (LipiAst *)node->as.items.items[i], depth);
            if (!item) return NULL;
            node->as.items.items[i] = item;
        }
    } else {
        LipiAst *item = expand_ast(c, (LipiAst *)node->as.items.items[2], depth);
        if (!item) return NULL;
        node->as.items.items[2] = item;
    }
    return node;
}

static LipiAst *expand_ast(LipiCompiler *c, LipiAst *node, int depth) {
    if (!node) {
        return NULL;
    }
    if (node->kind != AST_LIST) {
        return expand_children_generic(c, node, depth);
    }
    if (node->as.items.len == 0) {
        return node;
    }
    LipiAst *head = (LipiAst *)node->as.items.items[0];
    if (head->kind == AST_SYMBOL) {
        if (strcmp(head->as.text, "quote") == 0) {
            return node;
        }
        if (strcmp(head->as.text, "define") == 0) {
            return expand_define(c, node, depth);
        }
        if (strcmp(head->as.text, "#extern") == 0 || strcmp(head->as.text, "#inline") == 0) {
            return node;
        }
        LipiMacro *m = find_macro(c, head->as.text);
        if (m) {
            return apply_macro(c, m, node, depth);
        }
    }
    return expand_children_generic(c, node, depth);
}

int lipi_expand_macros(LipiCompiler *c) {
    LipiVec runtime_forms = {0};
    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        LipiMacro *m = NULL;
        int parsed = parse_macro_def(c, form, &m);
        if (parsed < 0) {
            return 0;
        }
        if (parsed > 0) {
            if (find_macro(c, m->name)) {
                lipi_diag_error(c, m->span, "duplicate macro definition", NULL);
                return 0;
            }
            lipi_vec_push(&c->macros, m);
        } else {
            lipi_vec_push(&runtime_forms, form);
        }
    }

    LipiVec expanded = {0};
    for (int i = 0; i < runtime_forms.len; i++) {
        LipiAst *form = expand_ast(c, (LipiAst *)runtime_forms.items[i], 0);
        if (!form) {
            return 0;
        }
        lipi_vec_push(&expanded, form);
    }
    c->forms = expanded;
    return !c->had_error;
}

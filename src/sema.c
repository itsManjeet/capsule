#include "lipi.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char *name;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    LipiSpan span;
} Local;

typedef struct {
    LipiCompiler *c;
    LipiVec locals; /* Local* */
    LipiVec named_lets; /* NamedLetInfo* */
} TypeCtx;

typedef struct {
    char *name;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    LipiSpan span;
} LetBindingInfo;

typedef struct {
    char *name;
    LipiVec bindings; /* LetBindingInfo* */
    LipiSpan span;
} NamedLetInfo;

static int is_list_head(LipiAst *node, const char *name) {
    return node && node->kind == AST_LIST && node->as.items.len > 0 &&
           lipi_ast_is_symbol((LipiAst *)node->as.items.items[0], name);
}

static Local *local_find(TypeCtx *ctx, const char *name) {
    for (int i = ctx->locals.len - 1; i >= 0; i--) {
        Local *local = (Local *)ctx->locals.items[i];
        if (strcmp(local->name, name) == 0) {
            return local;
        }
    }
    return NULL;
}

static NamedLetInfo *named_let_find(TypeCtx *ctx, const char *name) {
    for (int i = ctx->named_lets.len - 1; i >= 0; i--) {
        NamedLetInfo *info = (NamedLetInfo *)ctx->named_lets.items[i];
        if (strcmp(info->name, name) == 0) {
            return info;
        }
    }
    return NULL;
}

LipiSymbol *lipi_symbol_find(LipiCompiler *c, const char *name) {
    for (int i = 0; i < c->symbols.len; i++) {
        LipiSymbol *sym = (LipiSymbol *)c->symbols.items[i];
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static int add_symbol(LipiCompiler *c, LipiSymbol *sym) {
    LipiSymbol *existing = lipi_symbol_find(c, sym->name);
    if (existing) {
        char msg[256];
        snprintf(msg, sizeof(msg), "duplicate definition of symbol '%s'", sym->name);
        lipi_diag_error2(c, sym->span, msg, NULL, existing->span, "");
        return 0;
    }
    lipi_vec_push(&c->symbols, sym);
    return 1;
}

static int parse_typed_symbol(LipiCompiler *c, LipiAst *node, char **name, LipiTypeKind *type) {
    if (!node || node->kind != AST_SYMBOL) {
        lipi_diag_error(c, node ? node->span : (LipiSpan){ "<unknown>", 1, 1, 1 },
                        "expected typed symbol", "expected name:type");
        return 0;
    }
    char *type_name = NULL;
    if (!lipi_split_typed_name(c, node->as.text, name, &type_name)) {
        lipi_diag_error(c, node->span, "expected type annotation", "expected name:type");
        return 0;
    }
    *type = lipi_type_from_name(type_name);
    if (*type == TYPE_UNKNOWN) {
        char detail[256];
        snprintf(detail, sizeof(detail), "unknown type '%s'", type_name);
        lipi_diag_error(c, node->span, "unknown type", detail);
        return 0;
    }
    return 1;
}

static LipiParam *parse_param(LipiCompiler *c, LipiAst *node) {
    char *name = NULL;
    LipiTypeKind type = TYPE_UNKNOWN;
    if (!parse_typed_symbol(c, node, &name, &type)) {
        return NULL;
    }
    if (type == TYPE_VOID) {
        lipi_diag_error(c, node->span, "invalid parameter type", "parameters cannot have type void");
        return NULL;
    }
    LipiParam *p = (LipiParam *)lipi_arena_alloc(&c->arena, sizeof(LipiParam));
    p->name = name;
    p->type = type;
    p->elem_type = TYPE_UNKNOWN;
    p->span = node->span;
    return p;
}

static int parse_define(LipiCompiler *c, LipiAst *form) {
    if (!is_list_head(form, "define")) {
        return 0;
    }
    if (form->as.items.len < 3) {
        lipi_diag_error(c, form->span, "invalid define form", "expected variable or function definition");
        return -1;
    }
    LipiAst *target = (LipiAst *)form->as.items.items[1];
    if (target->kind == AST_LIST) {
        if (target->as.items.len < 1 || ((LipiAst *)target->as.items.items[0])->kind != AST_SYMBOL) {
            lipi_diag_error(c, target->span, "invalid function definition",
                            "expected (name:return-type args...)");
            return -1;
        }
        if (form->as.items.len < 3) {
            lipi_diag_error(c, form->span, "function definition requires a body", NULL);
            return -1;
        }
        char *name = NULL;
        LipiTypeKind ret = TYPE_UNKNOWN;
        if (!parse_typed_symbol(c, (LipiAst *)target->as.items.items[0], &name, &ret)) {
            return -1;
        }
        LipiFunction *fn = (LipiFunction *)lipi_arena_alloc(&c->arena, sizeof(LipiFunction));
        fn->name = name;
        fn->ret_type = ret;
        fn->span = ((LipiAst *)target->as.items.items[0])->span;
        fn->node = form;
        for (int i = 1; i < target->as.items.len; i++) {
            LipiParam *p = parse_param(c, (LipiAst *)target->as.items.items[i]);
            if (!p) return -1;
            for (int j = 0; j < fn->params.len; j++) {
                LipiParam *prev = (LipiParam *)fn->params.items[j];
                if (strcmp(prev->name, p->name) == 0) {
                    lipi_diag_error(c, p->span, "duplicate parameter", NULL);
                    return -1;
                }
            }
            lipi_vec_push(&fn->params, p);
        }
        if (fn->params.len > 6) {
            lipi_diag_error(c, target->span, "too many function parameters",
                            "x86_64-linux MVP supports at most 6 integer/pointer parameters");
            return -1;
        }
        for (int i = 2; i < form->as.items.len; i++) {
            lipi_vec_push(&fn->body, form->as.items.items[i]);
        }
        LipiSymbol *sym = (LipiSymbol *)lipi_arena_alloc(&c->arena, sizeof(LipiSymbol));
        sym->name = fn->name;
        sym->kind = SYM_FUNCTION;
        sym->type = fn->ret_type;
        sym->elem_type = TYPE_UNKNOWN;
        sym->params = fn->params;
        sym->node = form;
        sym->span = fn->span;
        if (!add_symbol(c, sym)) return -1;
        lipi_vec_push(&c->functions, fn);
        return 1;
    }

    if (form->as.items.len != 3) {
        lipi_diag_error(c, form->span, "invalid variable definition", "expected (define name:type value)");
        return -1;
    }
    char *name = NULL;
    LipiTypeKind type = TYPE_UNKNOWN;
    if (!parse_typed_symbol(c, target, &name, &type)) {
        return -1;
    }
    if (type == TYPE_VOID) {
        lipi_diag_error(c, target->span, "invalid variable type", "variables cannot have type void");
        return -1;
    }
    LipiGlobal *g = (LipiGlobal *)lipi_arena_alloc(&c->arena, sizeof(LipiGlobal));
    g->name = name;
    g->type = type;
    g->elem_type = TYPE_UNKNOWN;
    g->init = (LipiAst *)form->as.items.items[2];
    g->span = target->span;

    LipiSymbol *sym = (LipiSymbol *)lipi_arena_alloc(&c->arena, sizeof(LipiSymbol));
    sym->name = g->name;
    sym->kind = SYM_GLOBAL;
    sym->type = g->type;
    sym->elem_type = TYPE_UNKNOWN;
    sym->node = form;
    sym->span = g->span;
    if (!add_symbol(c, sym)) return -1;
    lipi_vec_push(&c->globals, g);
    return 1;
}

static int parse_extern(LipiCompiler *c, LipiAst *form) {
    if (!is_list_head(form, "#extern")) {
        return 0;
    }
    if (form->as.items.len != 2) {
        lipi_diag_error(c, form->span, "invalid #extern form",
                        "expected (#extern name:type) or (#extern (name:return-type args...))");
        return -1;
    }
    LipiAst *target = (LipiAst *)form->as.items.items[1];
    LipiSymbol *sym = (LipiSymbol *)lipi_arena_alloc(&c->arena, sizeof(LipiSymbol));
    sym->node = form;
    if (target->kind == AST_SYMBOL) {
        char *name = NULL;
        LipiTypeKind type = TYPE_UNKNOWN;
        if (!parse_typed_symbol(c, target, &name, &type)) return -1;
        sym->name = name;
        sym->kind = SYM_EXTERN_GLOBAL;
        sym->type = type;
        sym->elem_type = TYPE_UNKNOWN;
        sym->span = target->span;
    } else if (target->kind == AST_LIST && target->as.items.len >= 1) {
        char *name = NULL;
        LipiTypeKind ret = TYPE_UNKNOWN;
        if (!parse_typed_symbol(c, (LipiAst *)target->as.items.items[0], &name, &ret)) return -1;
        sym->name = name;
        sym->kind = SYM_EXTERN_FUNCTION;
        sym->type = ret;
        sym->elem_type = TYPE_UNKNOWN;
        sym->span = ((LipiAst *)target->as.items.items[0])->span;
        for (int i = 1; i < target->as.items.len; i++) {
            LipiParam *p = parse_param(c, (LipiAst *)target->as.items.items[i]);
            if (!p) return -1;
            lipi_vec_push(&sym->params, p);
        }
        if (sym->params.len > 6) {
            lipi_diag_error(c, target->span, "too many extern parameters",
                            "x86_64-linux MVP supports at most 6 integer/pointer parameters");
            return -1;
        }
    } else {
        lipi_diag_error(c, target->span, "invalid #extern target", NULL);
        return -1;
    }
    if (!add_symbol(c, sym)) return -1;
    return 1;
}

static int add_local(TypeCtx *ctx, const char *name, LipiTypeKind type, LipiTypeKind elem_type, LipiSpan span) {
    if (local_find(ctx, name)) {
        lipi_diag_error(ctx->c, span, "duplicate local definition", NULL);
        return 0;
    }
    Local *local = (Local *)lipi_arena_alloc(&ctx->c->arena, sizeof(Local));
    local->name = (char *)name;
    local->type = type;
    local->elem_type = elem_type;
    local->span = span;
    lipi_vec_push(&ctx->locals, local);
    return 1;
}

static LipiTypeKind type_expr(TypeCtx *ctx, LipiAst *expr);

static int expect_type(TypeCtx *ctx, LipiAst *expr, LipiTypeKind expected, const char *what) {
    LipiTypeKind got = type_expr(ctx, expr);
    if (got == TYPE_UNKNOWN) {
        return 0;
    }
    if (got != expected) {
        char msg[256];
        snprintf(msg, sizeof(msg), "type mismatch in '%s'", what);
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(expected), lipi_type_name(got));
        lipi_diag_error(ctx->c, expr->span, msg, detail);
        return 0;
    }
    return 1;
}

static int is_none_type(LipiTypeKind type) {
    return type == TYPE_VOID;
}

static int is_cast_name(const char *name, LipiTypeKind *expected_source, LipiTypeKind *target) {
    *expected_source = TYPE_UNKNOWN;
    *target = TYPE_UNKNOWN;

    LipiTypeKind direct = lipi_type_from_name(name);
    if (direct != TYPE_UNKNOWN && direct != TYPE_VOID) {
        *target = direct;
        return 1;
    }

    const char *arrow = strstr(name, "->");
    if (!arrow || arrow == name || arrow[2] == 0) {
        return 0;
    }
    char src_name[64];
    char dst_name[64];
    size_t src_len = (size_t)(arrow - name);
    size_t dst_len = strlen(arrow + 2);
    if (src_len >= sizeof(src_name) || dst_len >= sizeof(dst_name)) {
        return 0;
    }
    memcpy(src_name, name, src_len);
    src_name[src_len] = 0;
    memcpy(dst_name, arrow + 2, dst_len + 1);

    LipiTypeKind src = lipi_type_from_name(src_name);
    LipiTypeKind dst = lipi_type_from_name(dst_name);
    if (src == TYPE_UNKNOWN || dst == TYPE_UNKNOWN || dst == TYPE_VOID) {
        return 0;
    }
    *expected_source = src;
    *target = dst;
    return 1;
}

static int can_convert_type(LipiTypeKind from, LipiTypeKind to) {
    if (from == TYPE_UNKNOWN || to == TYPE_UNKNOWN || is_none_type(from) || is_none_type(to)) {
        return 0;
    }
    if (from == to) {
        return 1;
    }
    if ((lipi_type_is_integer(from) || from == TYPE_BOOL) &&
        (lipi_type_is_integer(to) || to == TYPE_BOOL)) {
        return 1;
    }
    if ((lipi_type_is_integer(from) && to == TYPE_PTR) ||
        (from == TYPE_PTR && lipi_type_is_integer(to))) {
        return 1;
    }
    if (from == TYPE_PTR && lipi_type_is_pointer_like(to)) {
        return 1;
    }
    if (lipi_type_is_pointer_like(from) && to == TYPE_PTR) {
        return 1;
    }
    if (lipi_type_is_pointer_like(from) && lipi_type_is_pointer_like(to)) {
        return 1;
    }
    return 0;
}

static int int_literal_fits_type(LipiAst *expr, LipiTypeKind target) {
    if (!expr || expr->kind != AST_INT || !lipi_type_is_integer(target)) {
        return 0;
    }
    int64_t value = expr->as.int_value;
    switch (target) {
    case TYPE_I8: return value >= -128 && value <= 127;
    case TYPE_I16: return value >= -32768 && value <= 32767;
    case TYPE_I32: return value >= -2147483647LL - 1 && value <= 2147483647LL;
    case TYPE_I64: return 1;
    case TYPE_U8: return value >= 0 && value <= 255;
    case TYPE_U16: return value >= 0 && value <= 65535;
    case TYPE_U32: return value >= 0 && value <= 4294967295LL;
    case TYPE_U64: return value >= 0;
    default: return 0;
    }
}

static int assignment_compatible(LipiTypeKind expected, LipiTypeKind got, LipiAst *value) {
    if (expected == got) {
        return 1;
    }
    if (int_literal_fits_type(value, expected)) {
        value->type = expected;
        return 1;
    }
    return 0;
}

static LipiTypeKind type_cast(TypeCtx *ctx, LipiAst *expr, const char *name,
                              LipiTypeKind expected_source, LipiTypeKind target) {
    if (expr->as.items.len != 2) {
        char detail[128];
        snprintf(detail, sizeof(detail), "'%s' expects 1 value", name);
        lipi_diag_error(ctx->c, expr->span, "invalid type conversion", detail);
        return TYPE_UNKNOWN;
    }
    LipiAst *value = (LipiAst *)expr->as.items.items[1];
    LipiTypeKind got = type_expr(ctx, value);
    if (got == TYPE_UNKNOWN) {
        return TYPE_UNKNOWN;
    }
    if (expected_source != TYPE_UNKNOWN && got != expected_source) {
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(expected_source), lipi_type_name(got));
        lipi_diag_error(ctx->c, value->span, "type mismatch in conversion", detail);
        return TYPE_UNKNOWN;
    }
    if (!can_convert_type(got, target)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "cannot convert %s to %s",
                 lipi_type_name(got), lipi_type_name(target));
        lipi_diag_error(ctx->c, value->span, "invalid type conversion", detail);
        return TYPE_UNKNOWN;
    }
    expr->type = target;
    expr->elem_type = value->elem_type;
    return target;
}

static LipiTypeKind type_define_expr(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 3 || ((LipiAst *)expr->as.items.items[1])->kind != AST_SYMBOL) {
        lipi_diag_error(ctx->c, expr->span, "invalid local define", "expected (define name:type value)");
        return TYPE_UNKNOWN;
    }
    char *name = NULL;
    LipiTypeKind declared = TYPE_UNKNOWN;
    if (!parse_typed_symbol(ctx->c, (LipiAst *)expr->as.items.items[1], &name, &declared)) {
        return TYPE_UNKNOWN;
    }
    LipiAst *value = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind got = type_expr(ctx, value);
    if (got != TYPE_UNKNOWN && !assignment_compatible(declared, got, value)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "type mismatch in definition of '%s'", name);
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(declared), lipi_type_name(got));
        lipi_diag_error(ctx->c, value->span, msg, detail);
        return TYPE_UNKNOWN;
    }
    if (!add_local(ctx, name, declared, value->elem_type, ((LipiAst *)expr->as.items.items[1])->span)) {
        return TYPE_UNKNOWN;
    }
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind type_begin(TypeCtx *ctx, LipiAst *expr) {
    LipiTypeKind result = TYPE_VOID;
    for (int i = 1; i < expr->as.items.len; i++) {
        result = type_expr(ctx, (LipiAst *)expr->as.items.items[i]);
    }
    expr->type = result;
    return result;
}

static int parse_binding_name(TypeCtx *ctx, LipiAst *node, char **name, LipiTypeKind *declared) {
    if (!node || node->kind != AST_SYMBOL) {
        lipi_diag_error(ctx->c, node ? node->span : (LipiSpan){ "<unknown>", 1, 1, 1 },
                        "invalid let binding", "expected symbol or name:type");
        return 0;
    }
    char *type_name = NULL;
    if (lipi_split_typed_name(ctx->c, node->as.text, name, &type_name)) {
        *declared = lipi_type_from_name(type_name);
        if (*declared == TYPE_UNKNOWN) {
            char detail[256];
            snprintf(detail, sizeof(detail), "unknown type '%s'", type_name);
            lipi_diag_error(ctx->c, node->span, "unknown type", detail);
            return 0;
        }
        if (*declared == TYPE_VOID) {
            lipi_diag_error(ctx->c, node->span, "invalid let binding type",
                            "let bindings cannot have type void");
            return 0;
        }
        return 1;
    }
    *name = node->as.text;
    *declared = TYPE_UNKNOWN;
    return 1;
}

static int let_name_seen(LipiVec *bindings, const char *name) {
    for (int i = 0; i < bindings->len; i++) {
        LetBindingInfo *info = (LetBindingInfo *)bindings->items[i];
        if (strcmp(info->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int collect_let_bindings(TypeCtx *ctx, LipiAst *bindings, LipiVec *out_infos) {
    LipiVec infos = {0};
    for (int i = 0; i < bindings->as.items.len; i++) {
        LipiAst *binding = (LipiAst *)bindings->as.items.items[i];
        if (!binding || binding->kind != AST_LIST || binding->as.items.len != 2) {
            lipi_diag_error(ctx->c, binding ? binding->span : bindings->span,
                            "invalid let binding", "expected (name value)");
            return 0;
        }

        LipiAst *name_node = (LipiAst *)binding->as.items.items[0];
        LipiAst *value = (LipiAst *)binding->as.items.items[1];
        char *name = NULL;
        LipiTypeKind declared = TYPE_UNKNOWN;
        if (!parse_binding_name(ctx, name_node, &name, &declared)) {
            return 0;
        }
        if (local_find(ctx, name) || let_name_seen(&infos, name)) {
            lipi_diag_error(ctx->c, name_node->span, "duplicate local definition", NULL);
            return 0;
        }

        LipiTypeKind got = type_expr(ctx, value);
        if (got == TYPE_UNKNOWN) {
            return 0;
        }
        if (got == TYPE_VOID) {
            lipi_diag_error(ctx->c, value->span, "invalid let binding",
                            "let binding value cannot be void");
            return 0;
        }
        if (declared != TYPE_UNKNOWN && !assignment_compatible(declared, got, value)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(declared), lipi_type_name(got));
            lipi_diag_error(ctx->c, value->span, "type mismatch in let binding", detail);
            return 0;
        }

        LetBindingInfo *info = (LetBindingInfo *)lipi_arena_alloc(&ctx->c->arena, sizeof(LetBindingInfo));
        info->name = name;
        info->type = declared == TYPE_UNKNOWN ? got : declared;
        info->elem_type = value->elem_type;
        info->span = name_node->span;
        lipi_vec_push(&infos, info);
    }
    *out_infos = infos;
    return 1;
}

static LipiTypeKind type_let(TypeCtx *ctx, LipiAst *expr) {
    int named = 0;
    char *let_name = NULL;
    LipiAst *bindings = NULL;
    int body_start = 2;

    if (expr->as.items.len >= 3 && ((LipiAst *)expr->as.items.items[1])->kind == AST_LIST) {
        bindings = (LipiAst *)expr->as.items.items[1];
    } else if (expr->as.items.len >= 4 &&
               ((LipiAst *)expr->as.items.items[1])->kind == AST_SYMBOL &&
               ((LipiAst *)expr->as.items.items[2])->kind == AST_LIST) {
        named = 1;
        let_name = ((LipiAst *)expr->as.items.items[1])->as.text;
        bindings = (LipiAst *)expr->as.items.items[2];
        body_start = 3;
    } else {
        lipi_diag_error(ctx->c, expr->span, "invalid let expression",
                        "expected (let ((name value)...) body...) or (let name ((name value)...) body...)");
        return TYPE_UNKNOWN;
    }

    if (named && (local_find(ctx, let_name) || named_let_find(ctx, let_name) ||
                  lipi_symbol_find(ctx->c, let_name))) {
        lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[1])->span,
                        "duplicate named let target", NULL);
        return TYPE_UNKNOWN;
    }

    LipiVec infos = {0};
    int base = ctx->locals.len;
    int named_base = ctx->named_lets.len;
    if (!collect_let_bindings(ctx, bindings, &infos)) {
        return TYPE_UNKNOWN;
    }

    for (int i = 0; i < infos.len; i++) {
        LetBindingInfo *info = (LetBindingInfo *)infos.items[i];
        if (!add_local(ctx, info->name, info->type, info->elem_type, info->span)) {
            ctx->locals.len = base;
            return TYPE_UNKNOWN;
        }
    }

    if (named) {
        NamedLetInfo *info = (NamedLetInfo *)lipi_arena_alloc(&ctx->c->arena, sizeof(NamedLetInfo));
        info->name = let_name;
        info->bindings = infos;
        info->span = ((LipiAst *)expr->as.items.items[1])->span;
        lipi_vec_push(&ctx->named_lets, info);
    }

    LipiTypeKind result = TYPE_VOID;
    for (int i = body_start; i < expr->as.items.len; i++) {
        result = type_expr(ctx, (LipiAst *)expr->as.items.items[i]);
        if (result == TYPE_UNKNOWN) {
            ctx->locals.len = base;
            ctx->named_lets.len = named_base;
            return TYPE_UNKNOWN;
        }
    }
    ctx->locals.len = base;
    ctx->named_lets.len = named_base;
    expr->type = result;
    return result;
}

static LipiTypeKind type_set(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 3 || ((LipiAst *)expr->as.items.items[1])->kind != AST_SYMBOL) {
        lipi_diag_error(ctx->c, expr->span, "invalid set expression",
                        "expected (set name value)");
        return TYPE_UNKNOWN;
    }
    LipiAst *name_node = (LipiAst *)expr->as.items.items[1];
    LipiTypeKind expected = TYPE_UNKNOWN;
    Local *local = local_find(ctx, name_node->as.text);
    if (local) {
        expected = local->type;
    } else {
        LipiSymbol *sym = lipi_symbol_find(ctx->c, name_node->as.text);
        if (sym && (sym->kind == SYM_GLOBAL || sym->kind == SYM_EXTERN_GLOBAL)) {
            expected = sym->type;
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unknown symbol '%s'", name_node->as.text);
            char detail[256];
            snprintf(detail, sizeof(detail), "symbol '%s' was not defined", name_node->as.text);
            lipi_diag_error(ctx->c, name_node->span, msg, detail);
            return TYPE_UNKNOWN;
        }
    }

    LipiAst *value = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind got = type_expr(ctx, value);
    if (got == TYPE_UNKNOWN) {
        return TYPE_UNKNOWN;
    }
    if (!assignment_compatible(expected, got, value)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(expected), lipi_type_name(got));
        lipi_diag_error(ctx->c, value->span, "type mismatch in set", detail);
        return TYPE_UNKNOWN;
    }
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind collection_elem_or_i64(LipiAst *collection) {
    return collection->elem_type == TYPE_UNKNOWN ? TYPE_I64 : collection->elem_type;
}

static LipiTypeKind type_len(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 2) {
        lipi_diag_error(ctx->c, expr->span, "invalid len expression", "expected (len value)");
        return TYPE_UNKNOWN;
    }
    LipiAst *value = (LipiAst *)expr->as.items.items[1];
    LipiTypeKind type = type_expr(ctx, value);
    if (type != TYPE_STR && type != TYPE_ARRAY && type != TYPE_MAP) {
        lipi_diag_error(ctx->c, value->span, "invalid len argument",
                        "len expects str, array/list, or map");
        return TYPE_UNKNOWN;
    }
    expr->type = TYPE_I64;
    return TYPE_I64;
}

static LipiTypeKind type_index(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 3) {
        lipi_diag_error(ctx->c, expr->span, "invalid index expression",
                        "expected (index collection key)");
        return TYPE_UNKNOWN;
    }
    LipiAst *collection = (LipiAst *)expr->as.items.items[1];
    LipiAst *key = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind collection_t = type_expr(ctx, collection);
    LipiTypeKind key_t = type_expr(ctx, key);
    if (collection_t == TYPE_STR) {
        if (!lipi_type_is_integer(key_t)) {
            lipi_diag_error(ctx->c, key->span, "invalid string index",
                            "string index must be an integer");
            return TYPE_UNKNOWN;
        }
        expr->type = TYPE_I64;
        return TYPE_I64;
    }
    if (collection_t == TYPE_ARRAY) {
        if (!lipi_type_is_integer(key_t)) {
            lipi_diag_error(ctx->c, key->span, "invalid array index",
                            "array/list index must be an integer");
            return TYPE_UNKNOWN;
        }
        expr->type = collection_elem_or_i64(collection);
        return expr->type;
    }
    if (collection_t == TYPE_MAP) {
        if (key_t != TYPE_STR) {
            lipi_diag_error(ctx->c, key->span, "invalid map index",
                            "map index must be str");
            return TYPE_UNKNOWN;
        }
        expr->type = collection_elem_or_i64(collection);
        return expr->type;
    }
    lipi_diag_error(ctx->c, collection->span, "invalid index target",
                    "index expects str, array/list, or map");
    return TYPE_UNKNOWN;
}

static LipiTypeKind type_append(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 3) {
        lipi_diag_error(ctx->c, expr->span, "invalid append expression",
                        "expected (append collection value)");
        return TYPE_UNKNOWN;
    }
    LipiAst *collection = (LipiAst *)expr->as.items.items[1];
    LipiAst *value = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind collection_t = type_expr(ctx, collection);
    LipiTypeKind value_t = type_expr(ctx, value);
    if (collection_t == TYPE_STR) {
        if (value_t != TYPE_STR && !lipi_type_is_integer(value_t)) {
            lipi_diag_error(ctx->c, value->span, "invalid string append value",
                            "string append expects str or integer byte value");
            return TYPE_UNKNOWN;
        }
        expr->type = TYPE_STR;
        return TYPE_STR;
    }
    if (collection_t == TYPE_ARRAY) {
        if (value_t == TYPE_VOID) {
            lipi_diag_error(ctx->c, value->span, "invalid array append value",
                            "array/list append value cannot be void");
            return TYPE_UNKNOWN;
        }
        LipiTypeKind elem = collection->elem_type;
        if (elem != TYPE_UNKNOWN && value_t != elem) {
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(elem), lipi_type_name(value_t));
            lipi_diag_error(ctx->c, value->span, "type mismatch in append", detail);
            return TYPE_UNKNOWN;
        }
        expr->type = TYPE_ARRAY;
        expr->elem_type = elem == TYPE_UNKNOWN ? value_t : elem;
        return TYPE_ARRAY;
    }
    lipi_diag_error(ctx->c, collection->span, "invalid append target",
                    "append expects str or array/list");
    return TYPE_UNKNOWN;
}

static LipiTypeKind type_for_each(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len < 4 || ((LipiAst *)expr->as.items.items[1])->kind != AST_SYMBOL) {
        lipi_diag_error(ctx->c, expr->span, "invalid for-each expression",
                        "expected (for-each name collection body...)");
        return TYPE_UNKNOWN;
    }
    LipiAst *name_node = (LipiAst *)expr->as.items.items[1];
    LipiAst *collection = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind collection_t = type_expr(ctx, collection);
    LipiTypeKind item_t = TYPE_UNKNOWN;
    if (collection_t == TYPE_STR) {
        item_t = TYPE_I64;
    } else if (collection_t == TYPE_ARRAY || collection_t == TYPE_MAP) {
        item_t = collection_elem_or_i64(collection);
    } else {
        lipi_diag_error(ctx->c, collection->span, "invalid for-each target",
                        "for-each expects str, array/list, or map");
        return TYPE_UNKNOWN;
    }

    int base = ctx->locals.len;
    if (!add_local(ctx, name_node->as.text, item_t, TYPE_UNKNOWN, name_node->span)) {
        return TYPE_UNKNOWN;
    }
    for (int i = 3; i < expr->as.items.len; i++) {
        LipiTypeKind body_t = type_expr(ctx, (LipiAst *)expr->as.items.items[i]);
        if (body_t == TYPE_UNKNOWN) {
            ctx->locals.len = base;
            return TYPE_UNKNOWN;
        }
    }
    ctx->locals.len = base;
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind type_assert(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 2) {
        lipi_diag_error(ctx->c, expr->span, "invalid assert expression",
                        "expected (assert condition)");
        return TYPE_UNKNOWN;
    }
    if (!expect_type(ctx, (LipiAst *)expr->as.items.items[1], TYPE_BOOL, "assert")) {
        return TYPE_UNKNOWN;
    }
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind type_assert_eq(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 3) {
        lipi_diag_error(ctx->c, expr->span, "invalid assert-eq expression",
                        "expected (assert-eq expected actual)");
        return TYPE_UNKNOWN;
    }
    LipiAst *expected = (LipiAst *)expr->as.items.items[1];
    LipiAst *actual = (LipiAst *)expr->as.items.items[2];
    LipiTypeKind expected_t = type_expr(ctx, expected);
    LipiTypeKind actual_t = type_expr(ctx, actual);
    if (expected_t == TYPE_UNKNOWN || actual_t == TYPE_UNKNOWN) {
        return TYPE_UNKNOWN;
    }
    if (expected_t == TYPE_VOID || actual_t == TYPE_VOID) {
        lipi_diag_error(ctx->c, expr->span, "invalid assert-eq expression",
                        "assert-eq cannot compare void values");
        return TYPE_UNKNOWN;
    }
    if (expected_t != actual_t && int_literal_fits_type(expected, actual_t)) {
        expected->type = actual_t;
        expected_t = actual_t;
    }
    if (!assignment_compatible(expected_t, actual_t, actual)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(expected_t), lipi_type_name(actual_t));
        lipi_diag_error(ctx->c, actual->span, "type mismatch in assert-eq", detail);
        return TYPE_UNKNOWN;
    }
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind type_if(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len != 4) {
        lipi_diag_error(ctx->c, expr->span, "invalid if expression", "expected (if condition then else)");
        return TYPE_UNKNOWN;
    }
    if (!expect_type(ctx, (LipiAst *)expr->as.items.items[1], TYPE_BOOL, "if")) {
        return TYPE_UNKNOWN;
    }
    LipiTypeKind then_t = type_expr(ctx, (LipiAst *)expr->as.items.items[2]);
    LipiTypeKind else_t = type_expr(ctx, (LipiAst *)expr->as.items.items[3]);
    if (then_t == TYPE_UNKNOWN || else_t == TYPE_UNKNOWN) return TYPE_UNKNOWN;
    if (then_t == TYPE_VOID && else_t == TYPE_VOID) {
        expr->type = TYPE_VOID;
        return TYPE_VOID;
    }
    if (then_t == TYPE_VOID) {
        expr->type = else_t;
        return else_t;
    }
    if (else_t == TYPE_VOID) {
        expr->type = then_t;
        return then_t;
    }
    if (then_t != else_t) {
        lipi_diag_error(ctx->c, expr->span, "type mismatch in 'if'",
                        "then and else branches must have matching types unless one branch is void");
        return TYPE_UNKNOWN;
    }
    expr->type = then_t;
    return then_t;
}

static LipiTypeKind type_builtin(TypeCtx *ctx, LipiAst *expr, const char *name) {
    if (expr->as.items.len != 3) {
        char detail[128];
        snprintf(detail, sizeof(detail), "'%s' expects 2 arguments", name);
        lipi_diag_error(ctx->c, expr->span, "invalid builtin call", detail);
        return TYPE_UNKNOWN;
    }
    if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
        strcmp(name, "*") == 0 || strcmp(name, "/") == 0) {
        LipiAst *a = (LipiAst *)expr->as.items.items[1];
        LipiAst *b = (LipiAst *)expr->as.items.items[2];
        LipiTypeKind at = type_expr(ctx, a);
        LipiTypeKind bt = type_expr(ctx, b);
        if (!lipi_type_is_integer(at) || !lipi_type_is_integer(bt)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "type mismatch in '%s'", name);
            lipi_diag_error(ctx->c, !lipi_type_is_integer(at) ? a->span : b->span, msg,
                            "arithmetic expects integer operands");
            return TYPE_UNKNOWN;
        }
        if (at != bt && int_literal_fits_type(b, at)) {
            b->type = at;
            bt = at;
        }
        if (at != bt && int_literal_fits_type(a, bt)) {
            a->type = bt;
            at = bt;
        }
        if (at != bt) {
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(at), lipi_type_name(bt));
            lipi_diag_error(ctx->c, b->span, "integer type mismatch", detail);
            return TYPE_UNKNOWN;
        }
        expr->type = at;
        return at;
    }
    if (strcmp(name, "=") == 0 || strcmp(name, "<") == 0 || strcmp(name, ">") == 0) {
        LipiAst *a = (LipiAst *)expr->as.items.items[1];
        LipiAst *b = (LipiAst *)expr->as.items.items[2];
        LipiTypeKind at = type_expr(ctx, a);
        LipiTypeKind bt = type_expr(ctx, b);
        if (!lipi_type_is_integer(at) || !lipi_type_is_integer(bt)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "type mismatch in '%s'", name);
            lipi_diag_error(ctx->c, !lipi_type_is_integer(at) ? a->span : b->span, msg,
                            "comparison expects integer operands");
            return TYPE_UNKNOWN;
        }
        if (at != bt && int_literal_fits_type(b, at)) {
            b->type = at;
            bt = at;
        }
        if (at != bt && int_literal_fits_type(a, bt)) {
            a->type = bt;
            at = bt;
        }
        if (at != bt) {
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(at), lipi_type_name(bt));
            lipi_diag_error(ctx->c, b->span, "integer type mismatch", detail);
            return TYPE_UNKNOWN;
        }
        expr->type = TYPE_BOOL;
        return TYPE_BOOL;
    }
    return TYPE_UNKNOWN;
}

static LipiTypeKind type_named_let_call(TypeCtx *ctx, LipiAst *expr, NamedLetInfo *info) {
    int argc = expr->as.items.len - 1;
    if (argc != 0 && argc != info->bindings.len) {
        char detail[128];
        snprintf(detail, sizeof(detail), "expected 0 or %d arguments but got %d",
                 info->bindings.len, argc);
        lipi_diag_error(ctx->c, expr->span, "wrong number of arguments in named let jump", detail);
        return TYPE_UNKNOWN;
    }
    for (int i = 0; i < argc; i++) {
        LetBindingInfo *binding = (LetBindingInfo *)info->bindings.items[i];
        LipiAst *arg = (LipiAst *)expr->as.items.items[i + 1];
        LipiTypeKind got = type_expr(ctx, arg);
        if (got == TYPE_UNKNOWN) {
            return TYPE_UNKNOWN;
        }
        if (got == TYPE_VOID) {
            lipi_diag_error(ctx->c, arg->span, "invalid use of void",
                            "void cannot be used as a named let jump argument");
            return TYPE_UNKNOWN;
        }
        if (!assignment_compatible(binding->type, got, arg)) {
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(binding->type), lipi_type_name(got));
            lipi_diag_error(ctx->c, arg->span, "type mismatch in named let jump", detail);
            return TYPE_UNKNOWN;
        }
    }
    expr->type = TYPE_VOID;
    return TYPE_VOID;
}

static LipiTypeKind type_call(TypeCtx *ctx, LipiAst *expr, const char *name) {
    LipiSymbol *sym = lipi_symbol_find(ctx->c, name);
    if (!sym) {
        char msg[256];
        snprintf(msg, sizeof(msg), "unknown symbol '%s'", name);
        char detail[256];
        snprintf(detail, sizeof(detail), "symbol '%s' was not defined", name);
        lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[0])->span, msg, detail);
        return TYPE_UNKNOWN;
    }
    if (sym->kind != SYM_FUNCTION && sym->kind != SYM_EXTERN_FUNCTION) {
        char msg[256];
        snprintf(msg, sizeof(msg), "symbol '%s' is not callable", name);
        lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[0])->span, msg, NULL);
        return TYPE_UNKNOWN;
    }
    int argc = expr->as.items.len - 1;
    if (argc != sym->params.len) {
        char msg[256];
        snprintf(msg, sizeof(msg), "wrong number of arguments in call to '%s'", name);
        char detail[128];
        snprintf(detail, sizeof(detail), "expected %d but got %d", sym->params.len, argc);
        lipi_diag_error(ctx->c, expr->span, msg, detail);
        return TYPE_UNKNOWN;
    }
    for (int i = 0; i < argc; i++) {
        LipiParam *p = (LipiParam *)sym->params.items[i];
        LipiAst *arg = (LipiAst *)expr->as.items.items[i + 1];
        LipiTypeKind got = type_expr(ctx, arg);
        if (got != TYPE_UNKNOWN && !assignment_compatible(p->type, got, arg)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "type mismatch in call to '%s'", name);
            char detail[256];
            snprintf(detail, sizeof(detail), "expected %s but got %s",
                     lipi_type_name(p->type), lipi_type_name(got));
            lipi_diag_error(ctx->c, arg->span, msg, detail);
            return TYPE_UNKNOWN;
        }
        if (got == TYPE_VOID) {
            lipi_diag_error(ctx->c, arg->span, "invalid use of void", "void cannot be passed as an argument");
            return TYPE_UNKNOWN;
        }
    }
    expr->type = sym->type;
    return sym->type;
}

static LipiTypeKind type_array(TypeCtx *ctx, LipiAst *expr) {
    LipiTypeKind elem = TYPE_UNKNOWN;
    for (int i = 0; i < expr->as.items.len; i++) {
        LipiTypeKind t = type_expr(ctx, (LipiAst *)expr->as.items.items[i]);
        if (t == TYPE_VOID) {
            lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[i])->span,
                            "invalid array element", "array elements cannot be void");
            return TYPE_UNKNOWN;
        }
        if (i == 0) {
            elem = t;
        } else if (!assignment_compatible(elem, t, (LipiAst *)expr->as.items.items[i])) {
            lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[i])->span,
                            "array element type mismatch", "array elements must all have the same type");
            return TYPE_UNKNOWN;
        }
    }
    expr->elem_type = elem;
    expr->type = TYPE_ARRAY;
    return TYPE_ARRAY;
}

static LipiTypeKind type_map(TypeCtx *ctx, LipiAst *expr) {
    if (expr->as.items.len % 2 != 0) {
        lipi_diag_error(ctx->c, expr->span, "invalid map literal", "map literals require key/value pairs");
        return TYPE_UNKNOWN;
    }
    LipiTypeKind elem = TYPE_UNKNOWN;
    for (int i = 0; i < expr->as.items.len; i += 2) {
        LipiAst *key = (LipiAst *)expr->as.items.items[i];
        if (key->kind != AST_STRING) {
            lipi_diag_error(ctx->c, key->span, "invalid map key", "MVP map keys must be strings");
            return TYPE_UNKNOWN;
        }
        LipiTypeKind value_t = type_expr(ctx, (LipiAst *)expr->as.items.items[i + 1]);
        if (value_t == TYPE_VOID) {
            lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[i + 1])->span,
                            "invalid map value", "map values cannot be void");
            return TYPE_UNKNOWN;
        }
        if (i == 0) {
            elem = value_t;
        } else if (!assignment_compatible(elem, value_t, (LipiAst *)expr->as.items.items[i + 1])) {
            lipi_diag_error(ctx->c, ((LipiAst *)expr->as.items.items[i + 1])->span,
                            "map value type mismatch", "map values must all have the same type");
            return TYPE_UNKNOWN;
        }
    }
    expr->elem_type = elem;
    expr->type = TYPE_MAP;
    return TYPE_MAP;
}

static LipiTypeKind type_expr(TypeCtx *ctx, LipiAst *expr) {
    if (!expr) return TYPE_UNKNOWN;
    switch (expr->kind) {
    case AST_INT:
        expr->type = TYPE_I64;
        return TYPE_I64;
    case AST_BOOL:
        expr->type = TYPE_BOOL;
        return TYPE_BOOL;
    case AST_STRING:
        expr->type = TYPE_STR;
        return TYPE_STR;
    case AST_BLOCK:
        expr->type = TYPE_VOID;
        return TYPE_VOID;
    case AST_SYMBOL: {
        if (strcmp(expr->as.text, "none") == 0 || strcmp(expr->as.text, "void") == 0) {
            expr->type = TYPE_VOID;
            return TYPE_VOID;
        }
        Local *local = local_find(ctx, expr->as.text);
        if (local) {
            expr->type = local->type;
            expr->elem_type = local->elem_type;
            return local->type;
        }
        LipiSymbol *sym = lipi_symbol_find(ctx->c, expr->as.text);
        if (sym && (sym->kind == SYM_GLOBAL || sym->kind == SYM_EXTERN_GLOBAL)) {
            expr->type = sym->type;
            expr->elem_type = sym->elem_type;
            return sym->type;
        }
        char msg[256];
        snprintf(msg, sizeof(msg), "unknown symbol '%s'", expr->as.text);
        char detail[256];
        snprintf(detail, sizeof(detail), "symbol '%s' was not defined", expr->as.text);
        lipi_diag_error(ctx->c, expr->span, msg, detail);
        return TYPE_UNKNOWN;
    }
    case AST_ARRAY:
        return type_array(ctx, expr);
    case AST_MAP:
        return type_map(ctx, expr);
    case AST_LIST:
        break;
    }
    if (expr->as.items.len == 0) {
        lipi_diag_error(ctx->c, expr->span, "empty list is not an expression", NULL);
        return TYPE_UNKNOWN;
    }
    LipiAst *head = (LipiAst *)expr->as.items.items[0];
    if (head->kind != AST_SYMBOL) {
        lipi_diag_error(ctx->c, head->span, "call target must be a symbol", NULL);
        return TYPE_UNKNOWN;
    }
    const char *name = head->as.text;
    if (strcmp(name, "define") == 0) return type_define_expr(ctx, expr);
    if (strcmp(name, "let") == 0) return type_let(ctx, expr);
    if (strcmp(name, "set") == 0) return type_set(ctx, expr);
    if (strcmp(name, "len") == 0) return type_len(ctx, expr);
    if (strcmp(name, "index") == 0) return type_index(ctx, expr);
    if (strcmp(name, "append") == 0) return type_append(ctx, expr);
    if (strcmp(name, "for-each") == 0) return type_for_each(ctx, expr);
    if (strcmp(name, "assert") == 0) return type_assert(ctx, expr);
    if (strcmp(name, "assert-eq") == 0) return type_assert_eq(ctx, expr);
    if (strcmp(name, "begin") == 0) return type_begin(ctx, expr);
    if (strcmp(name, "if") == 0) return type_if(ctx, expr);
    if (strcmp(name, "#inline") == 0) {
        if (expr->as.items.len != 3 ||
            ((LipiAst *)expr->as.items.items[1])->kind != AST_SYMBOL ||
            ((LipiAst *)expr->as.items.items[2])->kind != AST_BLOCK) {
            lipi_diag_error(ctx->c, expr->span, "invalid #inline form",
                            "expected (#inline asm ```...```) or (#inline c ```...```)");
            return TYPE_UNKNOWN;
        }
        expr->type = TYPE_VOID;
        return TYPE_VOID;
    }
    if (strcmp(name, "#extern") == 0) {
        expr->type = TYPE_VOID;
        return TYPE_VOID;
    }
    if (strcmp(name, "quote") == 0 || strcmp(name, "quasiquote") == 0 ||
        strcmp(name, "unquote") == 0 || strcmp(name, "unquote-splicing") == 0) {
        lipi_diag_error(ctx->c, expr->span, "quote form remained after macro expansion",
                        "quote and quasiquote are currently macro-time forms");
        return TYPE_UNKNOWN;
    }
    if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 || strcmp(name, "*") == 0 ||
        strcmp(name, "/") == 0 || strcmp(name, "=") == 0 || strcmp(name, "<") == 0 ||
        strcmp(name, ">") == 0) {
        return type_builtin(ctx, expr, name);
    }
    LipiTypeKind cast_source = TYPE_UNKNOWN;
    LipiTypeKind cast_target = TYPE_UNKNOWN;
    if (is_cast_name(name, &cast_source, &cast_target)) {
        return type_cast(ctx, expr, name, cast_source, cast_target);
    }
    NamedLetInfo *named_let = named_let_find(ctx, name);
    if (named_let) {
        return type_named_let_call(ctx, expr, named_let);
    }
    return type_call(ctx, expr, name);
}

static int type_global_init(LipiCompiler *c, LipiGlobal *g) {
    TypeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.c = c;
    LipiTypeKind got = type_expr(&ctx, g->init);
    if (got != TYPE_UNKNOWN && !assignment_compatible(g->type, got, g->init)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "type mismatch in definition of '%s'", g->name);
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but got %s",
                 lipi_type_name(g->type), lipi_type_name(got));
        lipi_diag_error(c, g->init->span, msg, detail);
        return 0;
    }
    g->elem_type = g->init->elem_type;
    LipiSymbol *sym = lipi_symbol_find(c, g->name);
    if (sym) {
        sym->elem_type = g->elem_type;
    }
    return got != TYPE_UNKNOWN;
}

static int type_function(LipiCompiler *c, LipiFunction *fn) {
    TypeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.c = c;
    for (int i = 0; i < fn->params.len; i++) {
        LipiParam *p = (LipiParam *)fn->params.items[i];
        if (!add_local(&ctx, p->name, p->type, p->elem_type, p->span)) {
            return 0;
        }
    }
    LipiTypeKind result = TYPE_VOID;
    for (int i = 0; i < fn->body.len; i++) {
        result = type_expr(&ctx, (LipiAst *)fn->body.items[i]);
        if (result == TYPE_UNKNOWN) return 0;
    }
    if (!assignment_compatible(fn->ret_type, result, (LipiAst *)fn->body.items[fn->body.len - 1])) {
        char msg[256];
        snprintf(msg, sizeof(msg), "function '%s' returns wrong type", fn->name);
        char detail[256];
        snprintf(detail, sizeof(detail), "expected %s but final expression has type %s",
                 lipi_type_name(fn->ret_type), lipi_type_name(result));
        lipi_diag_error(c, fn->span, msg, detail);
        return 0;
    }
    return 1;
}

static int type_top_forms(LipiCompiler *c) {
    TypeCtx top;
    memset(&top, 0, sizeof(top));
    top.c = c;
    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (is_list_head(form, "define")) {
            LipiAst *target = (LipiAst *)form->as.items.items[1];
            if (target->kind == AST_LIST) {
                form->type = TYPE_VOID;
                continue;
            }
            LipiGlobal *g = NULL;
            for (int j = 0; j < c->globals.len; j++) {
                LipiGlobal *candidate = (LipiGlobal *)c->globals.items[j];
                if (candidate->init == (LipiAst *)form->as.items.items[2]) {
                    g = candidate;
                    break;
                }
            }
            if (g && !type_global_init(c, g)) return 0;
            form->type = TYPE_VOID;
            continue;
        }
        if (is_list_head(form, "#extern")) {
            form->type = TYPE_VOID;
            continue;
        }
        LipiTypeKind t = type_expr(&top, form);
        if (t == TYPE_UNKNOWN) return 0;
    }
    return 1;
}

int lipi_sema_check(LipiCompiler *c) {
    c->symbols.len = 0;
    c->globals.len = 0;
    c->functions.len = 0;

    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        int def = parse_define(c, form);
        if (def < 0) return 0;
        if (def > 0) continue;
        int ext = parse_extern(c, form);
        if (ext < 0) return 0;
    }
    if (c->had_error) return 0;

    for (int i = 0; i < c->functions.len; i++) {
        if (!type_function(c, (LipiFunction *)c->functions.items[i])) {
            return 0;
        }
    }
    if (!type_top_forms(c)) {
        return 0;
    }
    return !c->had_error;
}

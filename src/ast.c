#include "lipi.h"

#include <string.h>

LipiAst *lipi_ast_new(LipiCompiler *c, LipiAstKind kind, LipiSpan span) {
    LipiAst *node = (LipiAst *)lipi_arena_alloc(&c->arena, sizeof(LipiAst));
    node->kind = kind;
    node->span = span;
    node->type = TYPE_UNKNOWN;
    node->elem_type = TYPE_UNKNOWN;
    return node;
}

LipiAst *lipi_ast_symbol(LipiCompiler *c, const char *name, LipiSpan span) {
    LipiAst *node = lipi_ast_new(c, AST_SYMBOL, span);
    node->as.text = lipi_arena_strdup(&c->arena, name);
    node->span.length = (int)strlen(name);
    return node;
}

LipiAst *lipi_ast_list2(LipiCompiler *c, const char *head, LipiAst *arg, LipiSpan span) {
    LipiAst *list = lipi_ast_new(c, AST_LIST, span);
    lipi_vec_push(&list->as.items, lipi_ast_symbol(c, head, span));
    lipi_vec_push(&list->as.items, arg);
    return list;
}

LipiAst *lipi_ast_clone(LipiCompiler *c, LipiAst *node) {
    if (!node) {
        return NULL;
    }
    LipiAst *copy = lipi_ast_new(c, node->kind, node->span);
    copy->type = node->type;
    copy->elem_type = node->elem_type;
    switch (node->kind) {
    case AST_INT:
        copy->as.int_value = node->as.int_value;
        break;
    case AST_BOOL:
        copy->as.bool_value = node->as.bool_value;
        break;
    case AST_STRING:
    case AST_SYMBOL:
    case AST_BLOCK:
        copy->as.text = lipi_arena_strdup(&c->arena, node->as.text ? node->as.text : "");
        break;
    case AST_LIST:
    case AST_ARRAY:
    case AST_MAP:
        for (int i = 0; i < node->as.items.len; i++) {
            lipi_vec_push(&copy->as.items, lipi_ast_clone(c, (LipiAst *)node->as.items.items[i]));
        }
        break;
    }
    return copy;
}

int lipi_ast_is_symbol(LipiAst *node, const char *name) {
    return node && node->kind == AST_SYMBOL && strcmp(node->as.text, name) == 0;
}

char *lipi_ast_symbol_name(LipiAst *node) {
    return (node && node->kind == AST_SYMBOL) ? node->as.text : NULL;
}

LipiAst *lipi_list_get(LipiAst *list, int index) {
    if (!list || list->kind != AST_LIST || index < 0 || index >= list->as.items.len) {
        return NULL;
    }
    return (LipiAst *)list->as.items.items[index];
}

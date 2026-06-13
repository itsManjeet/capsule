#include "lipi.h"

#include <stdio.h>
#include <string.h>

static int parse_expr(LipiParser *p, LipiAst **out);

static int parser_next(LipiParser *p) {
    return lipi_lexer_next(&p->lexer);
}

static LipiAst *wrap_reader_form(LipiParser *p, const char *name, LipiSpan span, LipiAst *arg) {
    LipiAst *list = lipi_ast_new(p->compiler, AST_LIST, span);
    lipi_vec_push(&list->as.items, lipi_ast_symbol(p->compiler, name, span));
    lipi_vec_push(&list->as.items, arg);
    return list;
}

static int parse_sequence(LipiParser *p, LipiAstKind kind, LipiTokenKind end, LipiSpan start, LipiAst **out) {
    LipiAst *node = lipi_ast_new(p->compiler, kind, start);
    if (!parser_next(p)) {
        return 0;
    }
    while (p->lexer.token.kind != end && p->lexer.token.kind != TOK_EOF) {
        LipiAst *item = NULL;
        if (!parse_expr(p, &item)) {
            return 0;
        }
        lipi_vec_push(&node->as.items, item);
    }
    if (p->lexer.token.kind == TOK_EOF) {
        const char *msg = end == TOK_RPAREN ? "expected ')' before end of file" :
                          end == TOK_RBRACK ? "expected ']' before end of file" :
                          "expected '}' before end of file";
        lipi_diag_error(p->compiler, start, msg, "unclosed list started here");
        return 0;
    }
    node->span.length = p->lexer.token.span.column - start.column + 1;
    if (!parser_next(p)) {
        return 0;
    }
    *out = node;
    return 1;
}

static int parse_expr(LipiParser *p, LipiAst **out) {
    LipiToken tok = p->lexer.token;
    LipiAst *node = NULL;
    switch (tok.kind) {
    case TOK_INT:
        node = lipi_ast_new(p->compiler, AST_INT, tok.span);
        node->as.int_value = tok.int_value;
        if (!parser_next(p)) return 0;
        *out = node;
        return 1;
    case TOK_BOOL:
        node = lipi_ast_new(p->compiler, AST_BOOL, tok.span);
        node->as.bool_value = tok.bool_value;
        if (!parser_next(p)) return 0;
        *out = node;
        return 1;
    case TOK_STRING:
        node = lipi_ast_new(p->compiler, AST_STRING, tok.span);
        node->as.text = tok.text;
        if (!parser_next(p)) return 0;
        *out = node;
        return 1;
    case TOK_SYMBOL:
    case TOK_ELLIPSIS:
    case TOK_COLON:
        node = lipi_ast_symbol(p->compiler, tok.text ? tok.text : ":", tok.span);
        if (!parser_next(p)) return 0;
        *out = node;
        return 1;
    case TOK_BLOCK:
        node = lipi_ast_new(p->compiler, AST_BLOCK, tok.span);
        node->as.text = tok.text;
        if (!parser_next(p)) return 0;
        *out = node;
        return 1;
    case TOK_LPAREN:
        return parse_sequence(p, AST_LIST, TOK_RPAREN, tok.span, out);
    case TOK_LBRACK:
        return parse_sequence(p, AST_ARRAY, TOK_RBRACK, tok.span, out);
    case TOK_LBRACE:
        return parse_sequence(p, AST_MAP, TOK_RBRACE, tok.span, out);
    case TOK_QUOTE:
        if (!parser_next(p)) return 0;
        if (!parse_expr(p, &node)) return 0;
        *out = wrap_reader_form(p, "quote", tok.span, node);
        return 1;
    case TOK_BACKQUOTE:
        if (!parser_next(p)) return 0;
        if (!parse_expr(p, &node)) return 0;
        *out = wrap_reader_form(p, "quasiquote", tok.span, node);
        return 1;
    case TOK_COMMA:
        if (!parser_next(p)) return 0;
        if (!parse_expr(p, &node)) return 0;
        *out = wrap_reader_form(p, "unquote", tok.span, node);
        return 1;
    case TOK_COMMA_AT:
        if (!parser_next(p)) return 0;
        if (!parse_expr(p, &node)) return 0;
        *out = wrap_reader_form(p, "unquote-splicing", tok.span, node);
        return 1;
    case TOK_RPAREN:
    case TOK_RBRACK:
    case TOK_RBRACE:
        lipi_diag_error(p->compiler, tok.span, "unexpected closing delimiter", NULL);
        return 0;
    case TOK_EOF:
        lipi_diag_error(p->compiler, tok.span, "unexpected end of file", NULL);
        return 0;
    }
    return 0;
}

int lipi_parse_source(LipiCompiler *c, LipiSource *source, LipiVec *out_forms) {
    LipiParser p;
    memset(&p, 0, sizeof(p));
    p.compiler = c;
    lipi_lexer_init(&p.lexer, c, source);
    if (!parser_next(&p)) {
        return 0;
    }
    while (p.lexer.token.kind != TOK_EOF) {
        LipiAst *form = NULL;
        if (!parse_expr(&p, &form)) {
            return 0;
        }
        lipi_vec_push(out_forms, form);
    }
    return !c->had_error;
}

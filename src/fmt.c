#define _POSIX_C_SOURCE 200809L
#include "lipi.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    FMT_ATOM,
    FMT_STRING,
    FMT_BLOCK,
    FMT_COMMENT,
    FMT_LIST,
    FMT_ARRAY,
    FMT_MAP,
    FMT_PREFIX
} FmtKind;

typedef struct FmtNode FmtNode;

struct FmtNode {
    FmtKind kind;
    LipiSpan span;
    char *text;
    char open_ch;
    char close_ch;
    LipiVec children; /* FmtNode* */
    FmtNode *child;
};

typedef struct {
    LipiCompiler *compiler;
    LipiSource *source;
    const char *text;
    size_t len;
    size_t pos;
    int line;
    int col;
} FmtParser;

static int fmt_peek(FmtParser *p) {
    if (p->pos >= p->len) {
        return 0;
    }
    return (unsigned char)p->text[p->pos];
}

static int fmt_peek_n(FmtParser *p, size_t n) {
    if (p->pos + n >= p->len) {
        return 0;
    }
    return (unsigned char)p->text[p->pos + n];
}

static int fmt_starts_with(FmtParser *p, const char *s) {
    size_t n = strlen(s);
    return p->pos + n <= p->len && memcmp(p->text + p->pos, s, n) == 0;
}

static int fmt_advance(FmtParser *p) {
    int ch = fmt_peek(p);
    if (!ch) {
        return 0;
    }
    p->pos++;
    if (ch == '\n') {
        p->line++;
        p->col = 1;
    } else {
        p->col++;
    }
    return ch;
}

static void fmt_skip_ws(FmtParser *p) {
    while (isspace((unsigned char)fmt_peek(p))) {
        fmt_advance(p);
    }
}

static LipiSpan fmt_span(FmtParser *p, int line, int col, size_t start) {
    int len = (int)(p->pos - start);
    if (len < 1) {
        len = 1;
    }
    return (LipiSpan){ p->source->path, line, col, len };
}

static FmtNode *fmt_new(FmtParser *p, FmtKind kind, int line, int col, size_t start) {
    FmtNode *node = (FmtNode *)lipi_arena_alloc(&p->compiler->arena, sizeof(FmtNode));
    node->kind = kind;
    node->span = fmt_span(p, line, col, start);
    return node;
}

static void fmt_error(FmtParser *p, LipiSpan span, const char *message, const char *detail) {
    lipi_diag_error(p->compiler, span, message, detail);
}

static int fmt_is_close(int ch) {
    return ch == ')' || ch == ']' || ch == '}';
}

static int fmt_is_delim(int ch) {
    return ch == 0 || isspace((unsigned char)ch) || ch == '(' || ch == ')' ||
           ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '\'' ||
           ch == '`' || ch == ',' || ch == '"' || ch == ';';
}

static FmtNode *fmt_parse_node(FmtParser *p);

static FmtNode *fmt_parse_comment(FmtParser *p) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    while (fmt_peek(p) && fmt_peek(p) != '\n') {
        fmt_advance(p);
    }
    FmtNode *node = fmt_new(p, FMT_COMMENT, line, col, start);
    node->text = lipi_arena_strndup(&p->compiler->arena, p->text + start, p->pos - start);
    return node;
}

static FmtNode *fmt_parse_string(FmtParser *p) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    fmt_advance(p);
    while (fmt_peek(p)) {
        int ch = fmt_advance(p);
        if (ch == '\\') {
            if (!fmt_peek(p)) {
                break;
            }
            fmt_advance(p);
            continue;
        }
        if (ch == '"') {
            FmtNode *node = fmt_new(p, FMT_STRING, line, col, start);
            node->text = lipi_arena_strndup(&p->compiler->arena, p->text + start, p->pos - start);
            return node;
        }
    }
    fmt_error(p, (LipiSpan){ p->source->path, line, col, 1 }, "unterminated string literal", NULL);
    return NULL;
}

static FmtNode *fmt_parse_block(FmtParser *p) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    fmt_advance(p);
    fmt_advance(p);
    fmt_advance(p);
    size_t body_start = p->pos;
    while (fmt_peek(p)) {
        if (fmt_starts_with(p, "```")) {
            size_t body_len = p->pos - body_start;
            fmt_advance(p);
            fmt_advance(p);
            fmt_advance(p);
            FmtNode *node = fmt_new(p, FMT_BLOCK, line, col, start);
            node->text = lipi_arena_strndup(&p->compiler->arena, p->text + body_start, body_len);
            return node;
        }
        fmt_advance(p);
    }
    fmt_error(p, (LipiSpan){ p->source->path, line, col, 3 },
              "unterminated triple-backtick block", NULL);
    return NULL;
}

static FmtNode *fmt_parse_atom(FmtParser *p) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    while (!fmt_is_delim(fmt_peek(p))) {
        fmt_advance(p);
    }
    FmtNode *node = fmt_new(p, FMT_ATOM, line, col, start);
    node->text = lipi_arena_strndup(&p->compiler->arena, p->text + start, p->pos - start);
    return node;
}

static FmtNode *fmt_parse_prefix(FmtParser *p) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    const char *prefix = NULL;
    int ch = fmt_peek(p);
    if (ch == '\'') {
        prefix = "'";
        fmt_advance(p);
    } else if (ch == '`') {
        prefix = "`";
        fmt_advance(p);
    } else if (ch == ',') {
        fmt_advance(p);
        if (fmt_peek(p) == '@') {
            fmt_advance(p);
            prefix = ",@";
        } else {
            prefix = ",";
        }
    }

    fmt_skip_ws(p);
    if (!fmt_peek(p) || fmt_is_close(fmt_peek(p))) {
        fmt_error(p, (LipiSpan){ p->source->path, line, col, (int)strlen(prefix) },
                  "expected expression after reader prefix", NULL);
        return NULL;
    }
    FmtNode *node = fmt_new(p, FMT_PREFIX, line, col, start);
    node->text = lipi_arena_strdup(&p->compiler->arena, prefix);
    node->child = fmt_parse_node(p);
    if (!node->child) {
        return NULL;
    }
    node->span = fmt_span(p, line, col, start);
    return node;
}

static FmtNode *fmt_parse_compound(FmtParser *p, FmtKind kind, char open_ch, char close_ch) {
    int line = p->line;
    int col = p->col;
    size_t start = p->pos;
    fmt_advance(p);

    FmtNode *node = fmt_new(p, kind, line, col, start);
    node->open_ch = open_ch;
    node->close_ch = close_ch;

    for (;;) {
        fmt_skip_ws(p);
        int ch = fmt_peek(p);
        if (!ch) {
            char message[96];
            snprintf(message, sizeof(message), "expected '%c' before end of file", close_ch);
            fmt_error(p, (LipiSpan){ p->source->path, line, col, 1 },
                      message, "unclosed form started here");
            return NULL;
        }
        if (ch == close_ch) {
            fmt_advance(p);
            node->span = fmt_span(p, line, col, start);
            return node;
        }
        if (fmt_is_close(ch)) {
            char message[96];
            snprintf(message, sizeof(message), "expected '%c' before '%c'", close_ch, ch);
            fmt_error(p, (LipiSpan){ p->source->path, p->line, p->col, 1 }, message, NULL);
            return NULL;
        }
        FmtNode *child = fmt_parse_node(p);
        if (!child) {
            return NULL;
        }
        lipi_vec_push(&node->children, child);
    }
}

static FmtNode *fmt_parse_node(FmtParser *p) {
    fmt_skip_ws(p);
    int ch = fmt_peek(p);
    if (!ch) {
        return NULL;
    }
    if (ch == ';') {
        return fmt_parse_comment(p);
    }
    if (ch == '"') {
        return fmt_parse_string(p);
    }
    if (ch == '`' && fmt_peek_n(p, 1) == '`' && fmt_peek_n(p, 2) == '`') {
        return fmt_parse_block(p);
    }
    if (ch == '\'' || ch == '`' || ch == ',') {
        return fmt_parse_prefix(p);
    }
    if (ch == '(') {
        return fmt_parse_compound(p, FMT_LIST, '(', ')');
    }
    if (ch == '[') {
        return fmt_parse_compound(p, FMT_ARRAY, '[', ']');
    }
    if (ch == '{') {
        return fmt_parse_compound(p, FMT_MAP, '{', '}');
    }
    if (fmt_is_close(ch)) {
        char message[64];
        snprintf(message, sizeof(message), "unexpected '%c'", ch);
        fmt_error(p, (LipiSpan){ p->source->path, p->line, p->col, 1 }, message, NULL);
        return NULL;
    }
    return fmt_parse_atom(p);
}

static int fmt_parse_all(FmtParser *p, LipiVec *out) {
    while (1) {
        fmt_skip_ws(p);
        if (!fmt_peek(p)) {
            return 1;
        }
        if (fmt_is_close(fmt_peek(p))) {
            char message[64];
            snprintf(message, sizeof(message), "unexpected '%c'", fmt_peek(p));
            fmt_error(p, (LipiSpan){ p->source->path, p->line, p->col, 1 }, message, NULL);
            return 0;
        }
        FmtNode *node = fmt_parse_node(p);
        if (!node) {
            return 0;
        }
        lipi_vec_push(out, node);
    }
}

static void fmt_indent(LipiStr *out, int indent) {
    for (int i = 0; i < indent; i++) {
        lipi_str_append_n(out, " ", 1);
    }
}

static int fmt_is_atom_text(FmtNode *node, const char *text) {
    return node && node->kind == FMT_ATOM && node->text && strcmp(node->text, text) == 0;
}

static FmtNode *fmt_child(FmtNode *node, int index) {
    if (!node || index < 0 || index >= node->children.len) {
        return NULL;
    }
    return (FmtNode *)node->children.items[index];
}

static int fmt_inline_len(FmtNode *node, int limit);

static int fmt_compound_inline_len(FmtNode *node, int limit) {
    int len = 2;
    for (int i = 0; i < node->children.len; i++) {
        if (i > 0) {
            len++;
        }
        int child_len = fmt_inline_len(fmt_child(node, i), limit - len);
        if (child_len < 0) {
            return -1;
        }
        len += child_len;
        if (len > limit) {
            return -1;
        }
    }
    return len;
}

static int fmt_inline_len(FmtNode *node, int limit) {
    if (!node || limit < 0) {
        return -1;
    }
    switch (node->kind) {
    case FMT_ATOM:
    case FMT_STRING: {
        int len = (int)strlen(node->text ? node->text : "");
        return len <= limit ? len : -1;
    }
    case FMT_PREFIX: {
        int prefix_len = (int)strlen(node->text ? node->text : "");
        int child_len = fmt_inline_len(node->child, limit - prefix_len);
        if (child_len < 0) {
            return -1;
        }
        return prefix_len + child_len;
    }
    case FMT_LIST:
    case FMT_ARRAY:
    case FMT_MAP:
        return fmt_compound_inline_len(node, limit);
    case FMT_BLOCK:
    case FMT_COMMENT:
        return -1;
    }
    return -1;
}

static int fmt_can_inline(FmtNode *node, int limit) {
    return fmt_inline_len(node, limit) >= 0;
}

static void fmt_render_node(LipiStr *out, FmtNode *node, int indent);

static void fmt_render_inline(LipiStr *out, FmtNode *node) {
    switch (node->kind) {
    case FMT_ATOM:
    case FMT_STRING:
        lipi_str_append(out, node->text ? node->text : "");
        break;
    case FMT_PREFIX:
        lipi_str_append(out, node->text ? node->text : "");
        fmt_render_inline(out, node->child);
        break;
    case FMT_LIST:
    case FMT_ARRAY:
    case FMT_MAP:
        lipi_str_append_n(out, &node->open_ch, 1);
        for (int i = 0; i < node->children.len; i++) {
            if (i > 0) {
                lipi_str_append_n(out, " ", 1);
            }
            fmt_render_inline(out, fmt_child(node, i));
        }
        lipi_str_append_n(out, &node->close_ch, 1);
        break;
    case FMT_BLOCK:
        lipi_str_append(out, "```");
        lipi_str_append(out, node->text ? node->text : "");
        lipi_str_append(out, "```");
        break;
    case FMT_COMMENT:
        lipi_str_append(out, node->text ? node->text : "");
        break;
    }
}

static void fmt_render_block_literal(LipiStr *out, FmtNode *block, int indent) {
    lipi_str_append(out, "```");
    const char *text = block->text ? block->text : "";
    size_t len = strlen(text);
    if (len == 0 || text[0] != '\n') {
        lipi_str_append_n(out, "\n", 1);
    }
    lipi_str_append(out, text);
    if (len == 0 || text[len - 1] != '\n') {
        lipi_str_append_n(out, "\n", 1);
    }
    fmt_indent(out, indent);
    lipi_str_append(out, "```");
}

static void fmt_render_inline_form(LipiStr *out, FmtNode *node, int indent) {
    fmt_indent(out, indent);
    fmt_render_inline(out, node);
}

static void fmt_render_define(LipiStr *out, FmtNode *node, int indent) {
    FmtNode *signature = fmt_child(node, 1);
    if ((!signature || signature->kind != FMT_LIST) &&
        fmt_can_inline(node, 80 - indent) && node->children.len <= 3) {
        fmt_render_inline_form(out, node, indent);
        return;
    }

    fmt_indent(out, indent);
    lipi_str_append(out, "(");
    fmt_render_inline(out, fmt_child(node, 0));
    if (node->children.len > 1 && fmt_can_inline(fmt_child(node, 1), 80 - indent - 8)) {
        lipi_str_append_n(out, " ", 1);
        fmt_render_inline(out, fmt_child(node, 1));
    }

    int body_start = 2;
    for (int i = body_start; i < node->children.len; i++) {
        lipi_str_append_n(out, "\n", 1);
        fmt_render_node(out, fmt_child(node, i), indent + 2);
    }
    lipi_str_append_n(out, ")", 1);
}

static void fmt_render_begin_like(LipiStr *out, FmtNode *node, int indent) {
    fmt_indent(out, indent);
    lipi_str_append(out, "(");
    fmt_render_inline(out, fmt_child(node, 0));
    for (int i = 1; i < node->children.len; i++) {
        lipi_str_append_n(out, "\n", 1);
        fmt_render_node(out, fmt_child(node, i), indent + 2);
    }
    if (node->children.len > 0 && fmt_child(node, node->children.len - 1)->kind == FMT_COMMENT) {
        lipi_str_append_n(out, "\n", 1);
        fmt_indent(out, indent);
    }
    lipi_str_append_n(out, ")", 1);
}

static void fmt_render_if(LipiStr *out, FmtNode *node, int indent) {
    if (fmt_can_inline(node, 80 - indent)) {
        fmt_render_inline_form(out, node, indent);
        return;
    }

    fmt_indent(out, indent);
    lipi_str_append(out, "(");
    fmt_render_inline(out, fmt_child(node, 0));
    if (node->children.len > 1 && fmt_can_inline(fmt_child(node, 1), 80 - indent - 5)) {
        lipi_str_append_n(out, " ", 1);
        fmt_render_inline(out, fmt_child(node, 1));
    }
    for (int i = 2; i < node->children.len; i++) {
        lipi_str_append_n(out, "\n", 1);
        fmt_render_node(out, fmt_child(node, i), indent + 2);
    }
    lipi_str_append_n(out, ")", 1);
}

static int fmt_binding_align(FmtNode *node, int indent, int binding_index) {
    int align = indent + 5;
    if (binding_index == 2 && fmt_child(node, 1) && fmt_child(node, 1)->kind == FMT_ATOM) {
        align += (int)strlen(fmt_child(node, 1)->text ? fmt_child(node, 1)->text : "") + 1;
    }
    return align;
}

static void fmt_render_binding_list(LipiStr *out, FmtNode *bindings, int align) {
    if (!bindings || bindings->kind != FMT_LIST || fmt_can_inline(bindings, 52)) {
        if (bindings) {
            fmt_render_inline(out, bindings);
        }
        return;
    }
    lipi_str_append_n(out, "(", 1);
    for (int i = 0; i < bindings->children.len; i++) {
        FmtNode *binding = fmt_child(bindings, i);
        if (i > 0) {
            lipi_str_append_n(out, "\n", 1);
            fmt_indent(out, align);
        }
        if (fmt_can_inline(binding, 80 - align)) {
            fmt_render_inline(out, binding);
        } else {
            fmt_render_node(out, binding, align);
        }
    }
    lipi_str_append_n(out, ")", 1);
}

static void fmt_render_let(LipiStr *out, FmtNode *node, int indent) {
    if (fmt_can_inline(node, 80 - indent)) {
        fmt_render_inline_form(out, node, indent);
        return;
    }

    fmt_indent(out, indent);
    lipi_str_append(out, "(let");
    int binding_index = 1;
    if (node->children.len > 2 && fmt_child(node, 1)->kind == FMT_ATOM && fmt_child(node, 2)->kind == FMT_LIST) {
        lipi_str_append_n(out, " ", 1);
        fmt_render_inline(out, fmt_child(node, 1));
        binding_index = 2;
    }
    if (node->children.len > binding_index) {
        lipi_str_append_n(out, " ", 1);
        fmt_render_binding_list(out, fmt_child(node, binding_index),
                                fmt_binding_align(node, indent, binding_index));
    }
    for (int i = binding_index + 1; i < node->children.len; i++) {
        lipi_str_append_n(out, "\n", 1);
        fmt_render_node(out, fmt_child(node, i), indent + 2);
    }
    lipi_str_append_n(out, ")", 1);
}

static void fmt_render_inline_directive(LipiStr *out, FmtNode *node, int indent) {
    FmtNode *kind = fmt_child(node, 1);
    FmtNode *block = fmt_child(node, 2);
    if (node->children.len == 3 && kind && block && kind->kind == FMT_ATOM && block->kind == FMT_BLOCK) {
        fmt_indent(out, indent);
        lipi_str_append(out, "(#inline ");
        lipi_str_append(out, kind->text ? kind->text : "");
        lipi_str_append_n(out, " ", 1);
        fmt_render_block_literal(out, block, indent);
        lipi_str_append_n(out, ")", 1);
        return;
    }
    fmt_render_begin_like(out, node, indent);
}

static void fmt_render_compound(LipiStr *out, FmtNode *node, int indent) {
    if (node->kind == FMT_LIST && node->children.len > 0) {
        FmtNode *head = fmt_child(node, 0);
        if (fmt_is_atom_text(head, "define") || fmt_is_atom_text(head, "define-macro")) {
            fmt_render_define(out, node, indent);
            return;
        }
        if (fmt_is_atom_text(head, "begin") || fmt_is_atom_text(head, "cond")) {
            fmt_render_begin_like(out, node, indent);
            return;
        }
        if (fmt_is_atom_text(head, "if")) {
            fmt_render_if(out, node, indent);
            return;
        }
        if (fmt_is_atom_text(head, "let")) {
            fmt_render_let(out, node, indent);
            return;
        }
        if (fmt_is_atom_text(head, "#inline")) {
            fmt_render_inline_directive(out, node, indent);
            return;
        }
    }

    if (fmt_can_inline(node, 80 - indent)) {
        fmt_render_inline_form(out, node, indent);
        return;
    }

    fmt_indent(out, indent);
    lipi_str_append_n(out, &node->open_ch, 1);
    if (node->children.len == 0) {
        lipi_str_append_n(out, &node->close_ch, 1);
        return;
    }

    FmtNode *first = fmt_child(node, 0);
    int start = 0;
    if (fmt_can_inline(first, 80 - indent - 1) && first->kind != FMT_COMMENT) {
        fmt_render_inline(out, first);
        start = 1;
    }

    for (int i = start; i < node->children.len; i++) {
        lipi_str_append_n(out, "\n", 1);
        fmt_render_node(out, fmt_child(node, i), indent + 2);
    }
    if (fmt_child(node, node->children.len - 1)->kind == FMT_COMMENT || start == 0) {
        lipi_str_append_n(out, "\n", 1);
        fmt_indent(out, indent);
    }
    lipi_str_append_n(out, &node->close_ch, 1);
}

static void fmt_render_prefix(LipiStr *out, FmtNode *node, int indent) {
    if (fmt_can_inline(node, 80 - indent)) {
        fmt_render_inline_form(out, node, indent);
        return;
    }
    fmt_indent(out, indent);
    lipi_str_append(out, node->text ? node->text : "");
    lipi_str_append_n(out, "\n", 1);
    fmt_render_node(out, node->child, indent + 2);
}

static void fmt_render_node(LipiStr *out, FmtNode *node, int indent) {
    switch (node->kind) {
    case FMT_ATOM:
    case FMT_STRING:
        fmt_indent(out, indent);
        lipi_str_append(out, node->text ? node->text : "");
        break;
    case FMT_COMMENT:
        fmt_indent(out, indent);
        lipi_str_append(out, node->text ? node->text : "");
        break;
    case FMT_BLOCK:
        fmt_indent(out, indent);
        fmt_render_block_literal(out, node, indent);
        break;
    case FMT_PREFIX:
        fmt_render_prefix(out, node, indent);
        break;
    case FMT_LIST:
    case FMT_ARRAY:
    case FMT_MAP:
        fmt_render_compound(out, node, indent);
        break;
    }
}

static void fmt_free_node_vectors(FmtNode *node) {
    if (!node) {
        return;
    }
    for (int i = 0; i < node->children.len; i++) {
        fmt_free_node_vectors((FmtNode *)node->children.items[i]);
    }
    fmt_free_node_vectors(node->child);
    free(node->children.items);
    node->children.items = NULL;
    node->children.len = 0;
    node->children.cap = 0;
}

static int fmt_write_file(LipiCompiler *c, const char *path, LipiStr *out) {
    LipiStr tmp = {0};
    lipi_str_appendf(&tmp, "%s.fmt.%ld.tmp", path, (long)getpid());
    FILE *f = fopen(tmp.data, "wb");
    if (!f) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", strerror(errno));
        lipi_diag_error(c, (LipiSpan){ path, 1, 1, 1 }, "could not create formatter temp file", detail);
        lipi_str_free(&tmp);
        return 0;
    }
    int ok = 1;
    if (out->len && fwrite(out->data, 1, out->len, f) != out->len) {
        ok = 0;
    }
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (!ok) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", strerror(errno));
        lipi_diag_error(c, (LipiSpan){ path, 1, 1, 1 }, "could not write formatted source", detail);
        unlink(tmp.data);
        lipi_str_free(&tmp);
        return 0;
    }
    if (rename(tmp.data, path) != 0) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", strerror(errno));
        lipi_diag_error(c, (LipiSpan){ path, 1, 1, 1 }, "could not replace source file", detail);
        unlink(tmp.data);
        lipi_str_free(&tmp);
        return 0;
    }
    lipi_str_free(&tmp);
    return 1;
}

static int fmt_format_source(LipiCompiler *c, LipiSource *source, LipiStr *out) {
    FmtParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.compiler = c;
    parser.source = source;
    parser.text = source->text;
    parser.len = source->len;
    parser.line = 1;
    parser.col = 1;

    LipiVec forms = {0};
    if (!fmt_parse_all(&parser, &forms)) {
        for (int i = 0; i < forms.len; i++) {
            fmt_free_node_vectors((FmtNode *)forms.items[i]);
        }
        free(forms.items);
        return 0;
    }

    for (int i = 0; i < forms.len; i++) {
        FmtNode *node = (FmtNode *)forms.items[i];
        if (i > 0) {
            FmtNode *prev = (FmtNode *)forms.items[i - 1];
            if (prev->kind == FMT_COMMENT || node->kind == FMT_COMMENT) {
                lipi_str_append_n(out, "\n", 1);
            } else {
                lipi_str_append_n(out, "\n\n", 2);
            }
        }
        fmt_render_node(out, node, 0);
    }
    if (out->len > 0 && out->data[out->len - 1] != '\n') {
        lipi_str_append_n(out, "\n", 1);
    }

    for (int i = 0; i < forms.len; i++) {
        fmt_free_node_vectors((FmtNode *)forms.items[i]);
    }
    free(forms.items);
    return 1;
}

int lipi_format_source_text(LipiCompiler *c, const char *path, const char *text, LipiStr *out) {
    LipiSource *source = lipi_source_from_text(c, path, text);
    return source && fmt_format_source(c, source, out);
}

int lipi_format_file(LipiCompiler *c, const char *path) {
    LipiSource *source = lipi_source_load(c, path);
    if (!source) {
        return 0;
    }

    LipiStr out = {0};
    if (!fmt_format_source(c, source, &out)) {
        lipi_str_free(&out);
        return 0;
    }

    int ok = fmt_write_file(c, path, &out);
    lipi_str_free(&out);
    return ok;
}

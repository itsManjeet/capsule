#include "lipi.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int lx_peek(LipiLexer *lx) {
    if (lx->pos >= (int)lx->source->len) {
        return 0;
    }
    return lx->source->text[lx->pos];
}

static int lx_peek_n(LipiLexer *lx, int n) {
    int p = lx->pos + n;
    if (p >= (int)lx->source->len) {
        return 0;
    }
    return lx->source->text[p];
}

static int lx_advance(LipiLexer *lx) {
    int ch = lx_peek(lx);
    if (!ch) {
        return 0;
    }
    lx->pos++;
    if (ch == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return ch;
}

static LipiSpan lx_span(LipiLexer *lx, int line, int col, int start) {
    LipiSpan sp = { lx->source->path, line, col, lx->pos - start };
    if (sp.length < 1) {
        sp.length = 1;
    }
    return sp;
}

static void lx_skip_ws(LipiLexer *lx) {
    for (;;) {
        int ch = lx_peek(lx);
        if (isspace(ch)) {
            lx_advance(lx);
            continue;
        }
        if (ch == ';') {
            while (lx_peek(lx) && lx_peek(lx) != '\n') {
                lx_advance(lx);
            }
            continue;
        }
        break;
    }
}

static int is_delim(int ch) {
    return ch == 0 || isspace(ch) || ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
           ch == '{' || ch == '}' || ch == '\'' || ch == '`' || ch == ',' ||
           ch == '"' || ch == ';';
}

static int read_string(LipiLexer *lx, LipiToken *tok, int start_line, int start_col, int start_pos) {
    LipiStr out = {0};
    lx_advance(lx); /* opening quote */
    while (lx_peek(lx) && lx_peek(lx) != '"') {
        int ch = lx_advance(lx);
        if (ch == '\\') {
            int esc = lx_advance(lx);
            switch (esc) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case '"': ch = '"'; break;
            case '\\': ch = '\\'; break;
            default: ch = esc; break;
            }
        }
        char c = (char)ch;
        lipi_str_append_n(&out, &c, 1);
    }
    if (lx_peek(lx) != '"') {
        lipi_diag_error(lx->compiler, (LipiSpan){ lx->source->path, start_line, start_col, 1 },
                        "unterminated string literal", NULL);
        lipi_str_free(&out);
        return 0;
    }
    lx_advance(lx);
    tok->kind = TOK_STRING;
    tok->span = lx_span(lx, start_line, start_col, start_pos);
    tok->text = lipi_arena_strdup(&lx->compiler->arena, out.data ? out.data : "");
    lipi_str_free(&out);
    return 1;
}

static int read_block(LipiLexer *lx, LipiToken *tok, int start_line, int start_col, int start_pos) {
    lx_advance(lx);
    lx_advance(lx);
    lx_advance(lx);
    LipiStr out = {0};
    while (lx_peek(lx)) {
        if (lx_peek(lx) == '`' && lx_peek_n(lx, 1) == '`' && lx_peek_n(lx, 2) == '`') {
            lx_advance(lx);
            lx_advance(lx);
            lx_advance(lx);
            tok->kind = TOK_BLOCK;
            tok->span = lx_span(lx, start_line, start_col, start_pos);
            tok->text = lipi_arena_strdup(&lx->compiler->arena, out.data ? out.data : "");
            lipi_str_free(&out);
            return 1;
        }
        char c = (char)lx_advance(lx);
        lipi_str_append_n(&out, &c, 1);
    }
    lipi_diag_error(lx->compiler, (LipiSpan){ lx->source->path, start_line, start_col, 3 },
                    "unterminated triple-backtick block", NULL);
    lipi_str_free(&out);
    return 0;
}

void lipi_lexer_init(LipiLexer *lx, LipiCompiler *c, LipiSource *source) {
    memset(lx, 0, sizeof(*lx));
    lx->source = source;
    lx->cur = source->text;
    lx->line = 1;
    lx->col = 1;
    lx->compiler = c;
}

int lipi_lexer_next(LipiLexer *lx) {
    lx_skip_ws(lx);
    LipiToken tok;
    memset(&tok, 0, sizeof(tok));
    int start_line = lx->line;
    int start_col = lx->col;
    int start_pos = lx->pos;
    int ch = lx_peek(lx);
    if (!ch) {
        tok.kind = TOK_EOF;
        tok.span = (LipiSpan){ lx->source->path, start_line, start_col, 1 };
        lx->token = tok;
        return 1;
    }

    switch (ch) {
    case '(':
        lx_advance(lx); tok.kind = TOK_LPAREN; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case ')':
        lx_advance(lx); tok.kind = TOK_RPAREN; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case '[':
        lx_advance(lx); tok.kind = TOK_LBRACK; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case ']':
        lx_advance(lx); tok.kind = TOK_RBRACK; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case '{':
        lx_advance(lx); tok.kind = TOK_LBRACE; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case '}':
        lx_advance(lx); tok.kind = TOK_RBRACE; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case '\'':
        lx_advance(lx); tok.kind = TOK_QUOTE; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    case '`':
        if (lx_peek_n(lx, 1) == '`' && lx_peek_n(lx, 2) == '`') {
            if (!read_block(lx, &tok, start_line, start_col, start_pos)) return 0;
        } else {
            lx_advance(lx); tok.kind = TOK_BACKQUOTE; tok.span = lx_span(lx, start_line, start_col, start_pos);
        }
        lx->token = tok;
        return 1;
    case ',':
        lx_advance(lx);
        if (lx_peek(lx) == '@') {
            lx_advance(lx);
            tok.kind = TOK_COMMA_AT;
        } else {
            tok.kind = TOK_COMMA;
        }
        tok.span = lx_span(lx, start_line, start_col, start_pos);
        lx->token = tok;
        return 1;
    case '"':
        if (!read_string(lx, &tok, start_line, start_col, start_pos)) return 0;
        lx->token = tok;
        return 1;
    case ':':
        lx_advance(lx); tok.kind = TOK_COLON; tok.span = lx_span(lx, start_line, start_col, start_pos); lx->token = tok; return 1;
    default:
        break;
    }

    if (ch == '.' && lx_peek_n(lx, 1) == '.' && lx_peek_n(lx, 2) == '.' && is_delim(lx_peek_n(lx, 3))) {
        lx_advance(lx);
        lx_advance(lx);
        lx_advance(lx);
        tok.kind = TOK_ELLIPSIS;
        tok.text = lipi_arena_strdup(&lx->compiler->arena, "...");
        tok.span = lx_span(lx, start_line, start_col, start_pos);
        lx->token = tok;
        return 1;
    }

    while (!is_delim(lx_peek(lx))) {
        lx_advance(lx);
    }
    int len = lx->pos - start_pos;
    char *text = lipi_arena_strndup(&lx->compiler->arena, lx->source->text + start_pos, (size_t)len);
    tok.span = lx_span(lx, start_line, start_col, start_pos);
    if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
        tok.kind = TOK_BOOL;
        tok.bool_value = strcmp(text, "true") == 0;
    } else {
        char *end = NULL;
        errno = 0;
        long long value = strtoll(text, &end, 10);
        if (!errno && end && *end == 0 && (isdigit((unsigned char)text[0]) ||
            ((text[0] == '-' || text[0] == '+') && isdigit((unsigned char)text[1])))) {
            tok.kind = TOK_INT;
            tok.int_value = value;
        } else {
            tok.kind = TOK_SYMBOL;
            tok.text = text;
        }
    }
    lx->token = tok;
    return 1;
}

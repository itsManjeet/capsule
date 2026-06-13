#include "lipi.h"

#include <stdio.h>
#include <string.h>

static LipiSource *diag_source(LipiCompiler *c, const char *file) {
    LipiSource *src = lipi_source_find(c, file);
    return src;
}

static void diag_store(LipiCompiler *c, LipiSpan span, const char *message, const char *detail,
                       int has_secondary, LipiSpan secondary, const char *secondary_message) {
    LipiDiag *diag = (LipiDiag *)lipi_arena_alloc(&c->arena, sizeof(LipiDiag));
    diag->primary = span;
    diag->secondary = secondary;
    diag->has_secondary = has_secondary;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "");
    snprintf(diag->detail, sizeof(diag->detail), "%s", detail ? detail : "");
    snprintf(diag->secondary_message, sizeof(diag->secondary_message), "%s",
             secondary_message ? secondary_message : "");
    lipi_vec_push(&c->diagnostics, diag);
}

static void render_line(LipiCompiler *c, LipiSpan sp, const char *secondary_message) {
    LipiSource *src = diag_source(c, sp.file);
    if (!src || sp.line <= 0 || sp.line > src->line_count) {
        return;
    }

    int start = src->line_offsets[sp.line - 1];
    int end = (sp.line == src->line_count) ? (int)src->len : src->line_offsets[sp.line] - 1;
    while (end > start && (src->text[end - 1] == '\n' || src->text[end - 1] == '\r')) {
        end--;
    }

    fprintf(stderr, "  %.*s\n", end - start, src->text + start);
    int col = sp.column < 1 ? 1 : sp.column;
    fprintf(stderr, "  ");
    for (int i = 1; i < col; i++) {
        char ch = src->text[start + i - 1];
        fputc(ch == '\t' ? '\t' : ' ', stderr);
    }
    int len = sp.length > 0 ? sp.length : 1;
    if (secondary_message && secondary_message[0]) {
        fputc('^', stderr);
        fprintf(stderr, " %s\n", secondary_message);
    } else {
        for (int i = 0; i < len; i++) {
            fputc('^', stderr);
        }
        fputc('\n', stderr);
    }
}

void lipi_diag_error(LipiCompiler *c, LipiSpan span, const char *message, const char *detail) {
    c->had_error = 1;
    diag_store(c, span, message, detail, 0, (LipiSpan){0}, NULL);
    if (c->quiet) {
        return;
    }
    fprintf(stderr, "%s:%d %s\n", span.file ? span.file : "<unknown>", span.line, message);
    render_line(c, span, NULL);
    if (detail && detail[0]) {
        fprintf(stderr, "%s\n", detail);
    }
}

void lipi_diag_error2(LipiCompiler *c, LipiSpan span, const char *message, const char *detail,
                      LipiSpan secondary, const char *secondary_message) {
    c->had_error = 1;
    diag_store(c, span, message, detail, 1, secondary, secondary_message);
    if (c->quiet) {
        return;
    }
    fprintf(stderr, "%s:%d %s\n", span.file ? span.file : "<unknown>", span.line, message);
    render_line(c, span, NULL);
    if (detail && detail[0]) {
        fprintf(stderr, "%s\n", detail);
    }
    fprintf(stderr, "previous definition here:\n");
    fprintf(stderr, "%s:%d\n", secondary.file ? secondary.file : "<unknown>", secondary.line);
    render_line(c, secondary, secondary_message);
}

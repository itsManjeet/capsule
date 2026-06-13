#define _POSIX_C_SOURCE 200809L
#include "lipi.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LipiSource *lipi_source_find(LipiCompiler *c, const char *path) {
    for (int i = 0; i < c->sources.len; i++) {
        LipiSource *src = (LipiSource *)c->sources.items[i];
        if (strcmp(src->path, path) == 0) {
            return src;
        }
    }
    return NULL;
}

static void source_build_line_offsets(LipiSource *src) {
    int cap = 32;
    src->line_offsets = (int *)calloc((size_t)cap, sizeof(int));
    if (!src->line_offsets) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    src->line_offsets[src->line_count++] = 0;
    for (int i = 0; i < (int)src->len; i++) {
        if (src->text[i] == '\n') {
            if (src->line_count == cap) {
                cap *= 2;
                int *lines = (int *)realloc(src->line_offsets, (size_t)cap * sizeof(int));
                if (!lines) {
                    fprintf(stderr, "lipi: out of memory\n");
                    exit(2);
                }
                src->line_offsets = lines;
            }
            src->line_offsets[src->line_count++] = i + 1;
        }
    }
}

LipiSource *lipi_source_from_text(LipiCompiler *c, const char *path, const char *text) {
    LipiSource *existing = lipi_source_find(c, path);
    if (existing) {
        return existing;
    }

    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    memcpy(copy, text, len + 1);

    LipiSource *src = (LipiSource *)lipi_arena_alloc(&c->arena, sizeof(LipiSource));
    src->path = lipi_arena_strdup(&c->arena, path);
    src->text = copy;
    src->len = len;
    source_build_line_offsets(src);
    lipi_vec_push(&c->sources, src);
    return src;
}

LipiSource *lipi_source_load(LipiCompiler *c, const char *path) {
    LipiSource *existing = lipi_source_find(c, path);
    if (existing) {
        return existing;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        LipiSpan sp = { path, 1, 1, 1 };
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", strerror(errno));
        lipi_diag_error(c, sp, "could not open source file", detail);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        lipi_diag_error(c, (LipiSpan){ path, 1, 1, 1 }, "could not read source file", NULL);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        lipi_diag_error(c, (LipiSpan){ path, 1, 1, 1 }, "could not read source file", NULL);
        return NULL;
    }
    rewind(f);

    char *text = (char *)malloc((size_t)n + 1);
    if (!text) {
        fclose(f);
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    size_t got = fread(text, 1, (size_t)n, f);
    fclose(f);
    text[got] = 0;

    LipiSource *src = (LipiSource *)lipi_arena_alloc(&c->arena, sizeof(LipiSource));
    src->path = lipi_arena_strdup(&c->arena, path);
    src->text = text;
    src->len = got;
    source_build_line_offsets(src);

    lipi_vec_push(&c->sources, src);
    return src;
}

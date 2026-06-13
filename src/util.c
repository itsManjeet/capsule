#define _POSIX_C_SOURCE 200809L
#include "lipi.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void lipi_vec_push(LipiVec *v, void *item) {
    if (v->len == v->cap) {
        int new_cap = v->cap ? v->cap * 2 : 8;
        void **items = (void **)realloc(v->items, (size_t)new_cap * sizeof(void *));
        if (!items) {
            fprintf(stderr, "lipi: out of memory\n");
            exit(2);
        }
        v->items = items;
        v->cap = new_cap;
    }
    v->items[v->len++] = item;
}

void lipi_vec_insert_vec(LipiVec *dst, LipiVec *src) {
    for (int i = 0; i < src->len; i++) {
        lipi_vec_push(dst, src->items[i]);
    }
}

void *lipi_arena_alloc(LipiArena *arena, size_t size) {
    size = (size + 7u) & ~7u;
    if (!arena->chunks || arena->chunks->used + size > arena->chunks->cap) {
        size_t cap = size > 4096 ? size : 4096;
        LipiArenaChunk *chunk = (LipiArenaChunk *)calloc(1, sizeof(LipiArenaChunk) + cap);
        if (!chunk) {
            fprintf(stderr, "lipi: out of memory\n");
            exit(2);
        }
        chunk->cap = cap;
        chunk->next = arena->chunks;
        arena->chunks = chunk;
    }
    void *ptr = arena->chunks->data + arena->chunks->used;
    arena->chunks->used += size;
    memset(ptr, 0, size);
    return ptr;
}

char *lipi_arena_strdup(LipiArena *arena, const char *s) {
    return lipi_arena_strndup(arena, s, strlen(s));
}

char *lipi_arena_strndup(LipiArena *arena, const char *s, size_t n) {
    char *out = (char *)lipi_arena_alloc(arena, n + 1);
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

void lipi_arena_free(LipiArena *arena) {
    LipiArenaChunk *chunk = arena->chunks;
    while (chunk) {
        LipiArenaChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    arena->chunks = NULL;
}

void lipi_str_append_n(LipiStr *s, const char *text, size_t n) {
    if (!n) {
        return;
    }
    if (s->len + n + 1 > s->cap) {
        size_t new_cap = s->cap ? s->cap * 2 : 256;
        while (new_cap < s->len + n + 1) {
            new_cap *= 2;
        }
        char *data = (char *)realloc(s->data, new_cap);
        if (!data) {
            fprintf(stderr, "lipi: out of memory\n");
            exit(2);
        }
        s->data = data;
        s->cap = new_cap;
    }
    memcpy(s->data + s->len, text, n);
    s->len += n;
    s->data[s->len] = 0;
}

void lipi_str_append(LipiStr *s, const char *text) {
    lipi_str_append_n(s, text, strlen(text));
}

void lipi_str_appendf(LipiStr *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        va_end(ap);
        return;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fprintf(stderr, "lipi: out of memory\n");
        exit(2);
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    va_end(ap);
    lipi_str_append_n(s, buf, (size_t)n);
    free(buf);
}

void lipi_str_free(LipiStr *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

int lipi_ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s);
    size_t b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

int lipi_contains_char(const char *s, char c) {
    return strchr(s, c) != NULL;
}

char *lipi_path_dirname(LipiArena *arena, const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return lipi_arena_strdup(arena, ".");
    }
    if (slash == path) {
        return lipi_arena_strdup(arena, "/");
    }
    return lipi_arena_strndup(arena, path, (size_t)(slash - path));
}

char *lipi_path_join(LipiArena *arena, const char *a, const char *b) {
    if (b[0] == '/') {
        return lipi_arena_strdup(arena, b);
    }
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    int need_slash = alen && a[alen - 1] != '/';
    char *out = (char *)lipi_arena_alloc(arena, alen + blen + (need_slash ? 2 : 1));
    memcpy(out, a, alen);
    if (need_slash) {
        out[alen++] = '/';
    }
    memcpy(out + alen, b, blen);
    out[alen + blen] = 0;
    return out;
}

char *lipi_mangle(LipiArena *arena, const char *name) {
    LipiStr out = {0};
    lipi_str_append(&out, "lipi_");
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (isalnum(*p) || *p == '_') {
            char ch = (char)*p;
            lipi_str_append_n(&out, &ch, 1);
        } else {
            lipi_str_appendf(&out, "_%02x", (unsigned int)*p);
        }
    }
    char *res = lipi_arena_strdup(arena, out.data ? out.data : "");
    lipi_str_free(&out);
    return res;
}

int lipi_run(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "%s terminated by signal %d\n", argv[0], WTERMSIG(status));
    }
    return 1;
}

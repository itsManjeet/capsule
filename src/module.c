#define _XOPEN_SOURCE 700
#include "lipi.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *path;
    int loading;
    int done;
} ModuleState;

static ModuleState *find_state(LipiCompiler *c, const char *path) {
    for (int i = 0; i < c->loaded_files.len; i++) {
        ModuleState *st = (ModuleState *)c->loaded_files.items[i];
        if (strcmp(st->path, path) == 0) {
            return st;
        }
    }
    return NULL;
}

static char *canonical_path(LipiCompiler *c, const char *path) {
    char buf[PATH_MAX];
    if (realpath(path, buf)) {
        return lipi_arena_strdup(&c->arena, buf);
    }
    return lipi_arena_strdup(&c->arena, path);
}

static int file_exists(const char *path) {
    return access(path, R_OK) == 0;
}

static char *with_lipi_ext(LipiCompiler *c, const char *name) {
    if (lipi_ends_with(name, ".lipi")) {
        return lipi_arena_strdup(&c->arena, name);
    }
    size_t n = strlen(name);
    char *out = (char *)lipi_arena_alloc(&c->arena, n + 6);
    memcpy(out, name, n);
    memcpy(out + n, ".lipi", 6);
    return out;
}

static char *expand_import_name(LipiCompiler *c, const char *name) {
    LipiStr out = {0};
    for (const char *p = name; *p;) {
        if (strncmp(p, "<arch>", 6) == 0) {
            lipi_str_append(&out, c->arch ? c->arch : "x86_64");
            p += 6;
        } else if (strncmp(p, "<platform>", 10) == 0) {
            lipi_str_append(&out, c->platform ? c->platform : "linux");
            p += 10;
        } else if (strncmp(p, "$arch", 5) == 0) {
            lipi_str_append(&out, c->arch ? c->arch : "x86_64");
            p += 5;
        } else if (strncmp(p, "$platform", 9) == 0) {
            lipi_str_append(&out, c->platform ? c->platform : "linux");
            p += 9;
        } else if (strncmp(p, "${arch}", 7) == 0) {
            lipi_str_append(&out, c->arch ? c->arch : "x86_64");
            p += 7;
        } else if (strncmp(p, "${platform}", 11) == 0) {
            lipi_str_append(&out, c->platform ? c->platform : "linux");
            p += 11;
        } else {
            lipi_str_append_n(&out, p, 1);
            p++;
        }
    }
    char *expanded = lipi_arena_strdup(&c->arena, out.data ? out.data : name);
    lipi_str_free(&out);
    return expanded;
}

static char *resolve_import(LipiCompiler *c, const char *importing_file, const char *name) {
    name = expand_import_name(c, name);
    char *dir = lipi_path_dirname(&c->arena, importing_file);
    int direct = lipi_contains_char(name, '/') || lipi_ends_with(name, ".lipi");
    if (direct) {
        char *candidate = lipi_path_join(&c->arena, dir, name);
        if (file_exists(candidate)) {
            return canonical_path(c, candidate);
        }
        return candidate;
    }

    char *file = with_lipi_ext(c, name);
    char *candidate = lipi_path_join(&c->arena, dir, file);
    if (file_exists(candidate)) {
        return canonical_path(c, candidate);
    }

    const char *env = getenv("LIPI_MODULE_PATH");
    if (env && env[0]) {
        const char *p = env;
        while (*p) {
            const char *colon = strchr(p, ':');
            size_t len = colon ? (size_t)(colon - p) : strlen(p);
            if (len) {
                char *part = lipi_arena_strndup(&c->arena, p, len);
                candidate = lipi_path_join(&c->arena, part, file);
                if (file_exists(candidate)) {
                    return canonical_path(c, candidate);
                }
            }
            if (!colon) {
                break;
            }
            p = colon + 1;
        }
    }

    if (c->stdlib_path && c->stdlib_path[0]) {
        candidate = lipi_path_join(&c->arena, c->stdlib_path, file);
        if (file_exists(candidate)) {
            return canonical_path(c, candidate);
        }
    }

    candidate = lipi_path_join(&c->arena, "/usr/lib/lipi", file);
    if (file_exists(candidate)) {
        return canonical_path(c, candidate);
    }
    return lipi_path_join(&c->arena, dir, file);
}

static int is_import_form(LipiAst *form, char **name) {
    if (!form || form->kind != AST_LIST || form->as.items.len != 2) {
        return 0;
    }
    if (!lipi_ast_is_symbol((LipiAst *)form->as.items.items[0], "#import")) {
        return 0;
    }
    LipiAst *arg = (LipiAst *)form->as.items.items[1];
    if (arg->kind != AST_STRING) {
        return -1;
    }
    *name = arg->as.text;
    return 1;
}

static void report_cycle(LipiCompiler *c, LipiSpan span, const char *target, LipiVec *stack) {
    LipiStr detail = {0};
    for (int i = 0; i < stack->len; i++) {
        const char *from = (const char *)stack->items[i];
        const char *to = (i + 1 < stack->len) ? (const char *)stack->items[i + 1] : target;
        lipi_str_appendf(&detail, "  %s imports %s\n", from, to);
    }
    lipi_diag_error(c, span, "import cycle detected", detail.data ? detail.data : NULL);
    lipi_str_free(&detail);
}

static int load_file(LipiCompiler *c, const char *path, LipiVec *out, LipiVec *stack) {
    char *canon = canonical_path(c, path);
    ModuleState *st = find_state(c, canon);
    if (st && st->done) {
        return 1;
    }
    if (st && st->loading) {
        report_cycle(c, (LipiSpan){ canon, 1, 1, 1 }, canon, stack);
        return 0;
    }
    if (!st) {
        st = (ModuleState *)lipi_arena_alloc(&c->arena, sizeof(ModuleState));
        st->path = canon;
        lipi_vec_push(&c->loaded_files, st);
    }
    st->loading = 1;
    lipi_vec_push(stack, st->path);

    LipiSource *src = lipi_source_load(c, st->path);
    if (!src) {
        return 0;
    }

    LipiVec raw = {0};
    if (!lipi_parse_source(c, src, &raw)) {
        return 0;
    }

    for (int i = 0; i < raw.len; i++) {
        LipiAst *form = (LipiAst *)raw.items[i];
        char *name = NULL;
        int imp = is_import_form(form, &name);
        if (imp < 0) {
            lipi_diag_error(c, form->span, "invalid #import form", "expected (#import \"module\")");
            return 0;
        }
        if (imp > 0) {
            char *target = resolve_import(c, st->path, name);
            ModuleState *target_state = find_state(c, target);
            if (target_state && target_state->loading) {
                report_cycle(c, form->span, target, stack);
                return 0;
            }
            if (!load_file(c, target, out, stack)) {
                return 0;
            }
            continue;
        }
        lipi_vec_push(out, form);
    }

    stack->len--;
    st->loading = 0;
    st->done = 1;
    return !c->had_error;
}

int lipi_load_program(LipiCompiler *c, const char *path) {
    LipiVec stack = {0};
    c->forms.len = 0;
    c->entry_file = canonical_path(c, path);
    return load_file(c, path, &c->forms, &stack);
}

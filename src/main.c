#define _POSIX_C_SOURCE 200809L
#include "lipi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  lipi [options] [input.lipi] [test-name]\n"
            "\n"
            "options:\n"
            "  -build output       build executable and do not run it\n"
            "  -asm output.s       emit assembly and stop\n"
            "  -check              parse, expand, resolve, and type-check only\n"
            "  -test               build and run entry-file test-* functions\n"
            "  -fmt                format input file in place\n"
            "  -lsp                start a stdio language server\n"
            "  -arch x86_64        target architecture\n"
            "  -platform linux     target platform\n");
}

static int split_target(char *target, const char **arch, const char **platform) {
    char *dash = strchr(target, '-');
    if (!dash || dash == target || dash[1] == 0) {
        return 0;
    }
    *dash = 0;
    *arch = target;
    *platform = dash + 1;
    return 1;
}

typedef enum {
    CLI_RUN,
    CLI_BUILD,
    CLI_ASM,
    CLI_CHECK,
    CLI_TEST,
    CLI_FMT,
    CLI_LSP
} CliMode;

typedef struct {
    CliMode mode;
    const char *input;
    const char *output;
    const char *test_filter;
    const char *arch;
    const char *platform;
} CliOptions;

static int set_mode(CliOptions *opts, CliMode mode) {
    if (opts->mode != CLI_RUN && opts->mode != mode) {
        usage();
        return 0;
    }
    opts->mode = mode;
    return 1;
}

static int parse_cli(int argc, char **argv, CliOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->mode = CLI_RUN;
    opts->arch = "x86_64";
    opts->platform = "linux";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-build") == 0) {
            if (!set_mode(opts, CLI_BUILD) || i + 1 >= argc) {
                usage();
                return 0;
            }
            opts->output = argv[++i];
        } else if (strcmp(argv[i], "-asm") == 0) {
            if (!set_mode(opts, CLI_ASM) || i + 1 >= argc) {
                usage();
                return 0;
            }
            opts->output = argv[++i];
        } else if (strcmp(argv[i], "-check") == 0) {
            if (!set_mode(opts, CLI_CHECK)) {
                return 0;
            }
        } else if (strcmp(argv[i], "-test") == 0) {
            if (!set_mode(opts, CLI_TEST)) {
                return 0;
            }
        } else if (strcmp(argv[i], "-fmt") == 0) {
            if (!set_mode(opts, CLI_FMT)) {
                return 0;
            }
        } else if (strcmp(argv[i], "-lsp") == 0) {
            if (!set_mode(opts, CLI_LSP)) {
                return 0;
            }
        } else if (strcmp(argv[i], "-arch") == 0 || strcmp(argv[i], "--arch") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 0;
            }
            opts->arch = argv[++i];
        } else if (strcmp(argv[i], "-platform") == 0 || strcmp(argv[i], "--platform") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 0;
            }
            opts->platform = argv[++i];
        } else if (strcmp(argv[i], "-target") == 0 || strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 0;
            }
            if (!split_target(argv[++i], &opts->arch, &opts->platform)) {
                usage();
                return 0;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            usage();
            return 0;
        } else if (!opts->input) {
            opts->input = argv[i];
        } else if (opts->mode == CLI_TEST && !opts->test_filter) {
            opts->test_filter = argv[i];
        } else {
            usage();
            return 0;
        }
    }

    if (!opts->input && opts->mode != CLI_LSP) {
        usage();
        return 0;
    }
    if (opts->input && opts->mode == CLI_LSP) {
        usage();
        return 0;
    }
    if ((opts->mode == CLI_BUILD || opts->mode == CLI_ASM) && !opts->output) {
        usage();
        return 0;
    }
    return 1;
}

static char *target_from_arch_platform(LipiCompiler *c) {
    LipiStr s = {0};
    lipi_str_appendf(&s, "%s-%s", c->arch, c->platform);
    char *target = lipi_arena_strdup(&c->arena, s.data ? s.data : "");
    lipi_str_free(&s);
    return target;
}

static const char *default_stdlib_path(LipiCompiler *c, const char *argv0) {
    const char *env = getenv("LIPI_STDLIB_PATH");
    if (env && env[0]) {
        return lipi_arena_strdup(&c->arena, env);
    }
    if (strchr(argv0, '/')) {
        char *dir = lipi_path_dirname(&c->arena, argv0);
        char *candidate = lipi_path_join(&c->arena, dir, "lib");
        if (access(candidate, R_OK) == 0) {
            return candidate;
        }
    }
    return "lib";
}

static int frontend(LipiCompiler *c, const char *input) {
    if (!lipi_load_program(c, input)) return 0;
    if (!lipi_expand_macros(c)) return 0;
    if (!lipi_sema_check(c)) return 0;
    lipi_collect_inline_c(c);
    return !c->had_error;
}

static void cleanup_tmp(const char *dir, const char *asm_path, const char *obj_path, const char *c_obj_path) {
    if (asm_path && asm_path[0]) unlink(asm_path);
    if (obj_path && obj_path[0]) unlink(obj_path);
    if (c_obj_path && c_obj_path[0]) unlink(c_obj_path);
    if (dir && dir[0]) {
        char c_path[1024];
        snprintf(c_path, sizeof(c_path), "%s/inline.c", dir);
        unlink(c_path);
        rmdir(dir);
    }
}

static int build_current_compiler(LipiCompiler *c, const char *output, const char *target) {
    char tmp_template[] = "/tmp/lipi-XXXXXX";
    char *tmpdir = mkdtemp(tmp_template);
    if (!tmpdir) {
        perror("mkdtemp");
        return 1;
    }
    char asm_path[1024];
    char obj_path[1024];
    char c_obj_path[1024] = {0};
    snprintf(asm_path, sizeof(asm_path), "%s/program.s", tmpdir);
    snprintf(obj_path, sizeof(obj_path), "%s/program.o", tmpdir);

    if (!lipi_emit_assembly(c, asm_path, target)) {
        cleanup_tmp(tmpdir, asm_path, obj_path, c_obj_path);
        return 1;
    }

    char *as_argv[] = { "as", "-o", obj_path, asm_path, NULL };
    if (lipi_run(as_argv) != 0) {
        fprintf(stderr, "lipi: assembler failed\n");
        cleanup_tmp(tmpdir, asm_path, obj_path, c_obj_path);
        return 1;
    }

    int c_status = lipi_compile_inline_c(c, tmpdir, c_obj_path, sizeof(c_obj_path));
    if (c_status < 0) {
        fprintf(stderr, "lipi: inline C compilation failed\n");
        cleanup_tmp(tmpdir, asm_path, obj_path, c_obj_path);
        return 1;
    }

    int link_status;
    if (c_status > 0) {
        char *ld_argv[] = { "ld", "-o", (char *)output, obj_path, c_obj_path, NULL };
        link_status = lipi_run(ld_argv);
    } else {
        char *ld_argv[] = { "ld", "-o", (char *)output, obj_path, NULL };
        link_status = lipi_run(ld_argv);
    }
    if (link_status != 0) {
        fprintf(stderr, "lipi: linker failed\n");
        cleanup_tmp(tmpdir, asm_path, obj_path, c_obj_path);
        return 1;
    }

    cleanup_tmp(tmpdir, asm_path, obj_path, c_obj_path);
    return 0;
}

static int command_build(LipiCompiler *c, const char *input, const char *output, const char *target) {
    if (!output) {
        usage();
        return 1;
    }
    if (!frontend(c, input)) {
        return 1;
    }
    return build_current_compiler(c, output, target);
}

static int command_emit_asm(LipiCompiler *c, const char *input, const char *output, const char *target) {
    if (!output) {
        usage();
        return 1;
    }
    if (!frontend(c, input)) {
        return 1;
    }
    return lipi_emit_assembly(c, output, target) ? 0 : 1;
}

static int command_check(LipiCompiler *c, const char *input) {
    return frontend(c, input) ? 0 : 1;
}

static int command_format(LipiCompiler *c, const char *input) {
    return lipi_format_file(c, input) ? 0 : 1;
}

static int command_run(LipiCompiler *c, const char *input, const char *target) {
    if (!frontend(c, input)) {
        return 1;
    }

    char tmp_template[] = "/tmp/lipi-run-XXXXXX";
    char *tmpdir = mkdtemp(tmp_template);
    if (!tmpdir) {
        perror("mkdtemp");
        return 1;
    }
    char exe_path[1024];
    snprintf(exe_path, sizeof(exe_path), "%s/program", tmpdir);
    int build_rc = build_current_compiler(c, exe_path, target);
    if (build_rc != 0) {
        rmdir(tmpdir);
        return build_rc;
    }

    char *run_argv[] = { exe_path, NULL };
    int run_rc = lipi_run(run_argv);
    unlink(exe_path);
    rmdir(tmpdir);
    return run_rc;
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int is_entry_test_function(LipiCompiler *c, LipiFunction *fn) {
    if (!starts_with(fn->name, "test-")) {
        return 0;
    }
    if (c->entry_file && fn->span.file && strcmp(c->entry_file, fn->span.file) != 0) {
        return 0;
    }
    if (c->test_filter && strcmp(c->test_filter, fn->name) != 0) {
        return 0;
    }
    return 1;
}

static int validate_tests(LipiCompiler *c) {
    int count = 0;
    for (int i = 0; i < c->functions.len; i++) {
        LipiFunction *fn = (LipiFunction *)c->functions.items[i];
        if (!is_entry_test_function(c, fn)) {
            continue;
        }
        count++;
        if (fn->params.len != 0) {
            lipi_diag_error(c, fn->span, "invalid test function",
                            "test functions must not take arguments");
            return -1;
        }
        if (fn->ret_type != TYPE_VOID && fn->ret_type != TYPE_BOOL && !lipi_type_is_integer(fn->ret_type)) {
            lipi_diag_error(c, fn->span, "invalid test function return type",
                            "test functions must return none, bool, or an integer type");
            return -1;
        }
    }
    if (count == 0) {
        if (c->test_filter) {
            fprintf(stderr, "lipi: no test named '%s' in %s\n", c->test_filter, c->entry_file);
        } else {
            fprintf(stderr, "lipi: no tests found in %s\n", c->entry_file);
        }
        return -1;
    }
    return count;
}

static int command_test(LipiCompiler *c, const char *input, const char *filter, const char *target) {
    c->test_mode = 1;
    c->test_filter = filter;
    if (!frontend(c, input)) {
        return 1;
    }
    int count = validate_tests(c);
    if (count < 0) {
        return 1;
    }

    char tmp_template[] = "/tmp/lipi-test-XXXXXX";
    char *tmpdir = mkdtemp(tmp_template);
    if (!tmpdir) {
        perror("mkdtemp");
        return 1;
    }
    char exe_path[1024];
    snprintf(exe_path, sizeof(exe_path), "%s/tests", tmpdir);
    int build_rc = build_current_compiler(c, exe_path, target);
    if (build_rc != 0) {
        rmdir(tmpdir);
        return build_rc;
    }

    char *run_argv[] = { exe_path, NULL };
    int run_rc = lipi_run(run_argv);
    if (run_rc == 0) {
        printf("ok %d test%s\n", count, count == 1 ? "" : "s");
    } else {
        fprintf(stderr, "FAIL %d test%s\n", count, count == 1 ? "" : "s");
    }
    unlink(exe_path);
    rmdir(tmpdir);
    return run_rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }

    CliOptions opts;
    if (!parse_cli(argc, argv, &opts)) {
        return 1;
    }

    LipiCompiler compiler;
    memset(&compiler, 0, sizeof(compiler));
    compiler.arch = opts.arch;
    compiler.platform = opts.platform;
    compiler.stdlib_path = default_stdlib_path(&compiler, argv[0]);
    compiler.target = target_from_arch_platform(&compiler);

    int rc = 1;
    switch (opts.mode) {
    case CLI_RUN:
        rc = command_run(&compiler, opts.input, compiler.target);
        break;
    case CLI_BUILD:
        rc = command_build(&compiler, opts.input, opts.output, compiler.target);
        break;
    case CLI_ASM:
        rc = command_emit_asm(&compiler, opts.input, opts.output, compiler.target);
        break;
    case CLI_CHECK:
        rc = command_check(&compiler, opts.input);
        break;
    case CLI_TEST:
        rc = command_test(&compiler, opts.input, opts.test_filter, compiler.target);
        break;
    case CLI_FMT:
        rc = command_format(&compiler, opts.input);
        break;
    case CLI_LSP:
        rc = lipi_lsp_run(&compiler);
        break;
    }

    for (int i = 0; i < compiler.sources.len; i++) {
        LipiSource *src = (LipiSource *)compiler.sources.items[i];
        free(src->text);
        free(src->line_offsets);
    }
    free(compiler.sources.items);
    free(compiler.forms.items);
    free(compiler.symbols.items);
    free(compiler.globals.items);
    free(compiler.functions.items);
    free(compiler.macros.items);
    free(compiler.inline_c.items);
    free(compiler.loaded_files.items);
    free(compiler.diagnostics.items);
    lipi_arena_free(&compiler.arena);
    return rc;
}

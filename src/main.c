#include <runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "_priv.h"

#ifdef HAS_READLINE
#include <readline/readline.h>
#else

static char buffer[2048];
char *readline(const char *prompt) {
    fputs(prompt, stdout);
    fgets(buffer, sizeof(buffer), stdin);
    return strdup(buffer);
}

#endif

int is_complete(const char *source) {
    if (source == NULL) return 0;
    int count = 0;
    for (const char *i = source; *i != '\0'; i++) {
        switch (*i) {
            case '(':
                count++;
                break;
            case ')':
                count--;
                break;
        }
    }
    return count == 0;
}

int main(int argc, char **argv) {
    char *source = NULL;
    const char *filename = NULL;
    Capsule scope = Capsule_scope_new(Capsule_nil);
    Capsule result;
    define_builtins(scope);
    int dump_ast = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--dump-ast") == 0)
                dump_ast = 1;
            else {
                fprintf(stderr, "invalid flag '%s'\n", argv[i]);
                return 1;
            }
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
        }
    }

    if (filename) {
        source = readfile(filename);
        if (source == NULL) {
            perror("readfile");
            return 1;
        }
    }

    result = Capsule_eval(RUNTIME, scope);
    if (CAPSULE_ERRORP(result)) {
        print_error("runtime.cap", RUNTIME, result);
    }

    do {
        if (filename == NULL) {
            source = readline("> ");
            while (!is_complete(source)) {
                char *line = readline("... ");
                size_t source_len = strlen(source);
                size_t line_len = strlen(line);
                size_t new_len = source_len + line_len + 1;
                char *new_source = malloc(new_len + 1);
                snprintf(new_source, new_len + 1, "%s %s", source, line);
                free(source);
                free(line);
                source = new_source;
            }
        }
        result = Capsule_eval(source, scope);
        if (CAPSULE_ERRORP(result)) {
            print_error(filename ? filename : "stdin", source, result);
            if (filename) return 1;
        } else {
            fprintf(stdout, ":: ");
            Capsule_print(result, stdout);
            fprintf(stdout, "\n");
        }

        free(source);
        source = NULL;
    } while (filename == NULL);
    return 0;
}

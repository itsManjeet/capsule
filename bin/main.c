/*
* Copyright (c) 2024 Manjeet Singh <itsmanjeet1998@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../src/priv.h"
#include "capsule.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef HAS_READLINE
#    include <readline/history.h>
#    include <readline/readline.h>
#endif

static int is_complete(const char* source) {
    if (source == NULL || strlen(source) == 0) return 0;
    int count[3] = {0};
    while (*source != '\0') {
        switch (*source++) {
        case '(': count[0]++; break;
        case '{': count[1]++; break;
        case '[': count[2]++; break;
        case ')': count[0]--; break;
        case '}': count[1]--; break;
        case ']': count[2]--; break;
        default: break;
        }
    }

    return count[0] == count[1] == count[2] == 0;
}

int main(int argc, char** argv) {
    Capsule scope = Capsule_Scope_global();
    const char* filename = NULL;
    char* source = NULL;
    int interactive = 0;
    CapsuleError error = CAPSULE_ERROR_NONE;
    Capsule result;
    Capsule args = {CAPSULE_TYPE_NIL};
    Capsule args_i = args;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "ERROR: invalid flag '%s'\n", argv[i]);
            return 1;
        } else if (filename == NULL && access(argv[i], F_OK) == 0) {
            filename = argv[i];
        } else {
            if (CAPSULE_NILP(args_i)) {
                args = args_i =
                        CAPSULE_CONS(CAPSULE_STRING(argv[i]), Capsule_nil);
            } else {
                CAPSULE_CDR(args_i) =
                        CAPSULE_CONS(CAPSULE_STRING(argv[i]), Capsule_nil);
                args_i = CAPSULE_CDR(args_i);
            }
        }
    }

    Capsule_Scope_define(scope, CAPSULE_SYMBOL("ARGS"), args);

    if (filename) {
        source = slurp(filename);
        if (source == NULL) {
            fprintf(stderr, "ERROR: failed to read '%s': %s\n", filename,
                    strerror(errno));
            return 1;
        }
    } else {
        printf("%s\n"
               "Capsule Programming Language\n"
               "  CTRL+C to exit\n",
                Capsule_logo());
        interactive = 1;
    }

    do {
        if (interactive) {
            source = readline(">> ");
            while (!is_complete(source)) {
                char* remaining = readline("... ");
                size_t source_len = strlen(source);
                size_t remaining_len = strlen(remaining);

                source = realloc(source,
                        sizeof(char) * (source_len + remaining_len + 2));
                strcat(source, " ");
                strcat(source, remaining);
                free(remaining);
            }

            add_history(source);
        }

        if ((error = Capsule_eval(source, scope, &result))) {
            printf("ERROR: %s\n", Capsule_Error_str(error));
        } else {
            if (interactive) {
                Capsule_print(result, stdout);
                fprintf(stdout, "\n");
            } else
                return 0;
        }

        free(source);
        source = NULL;
    } while (interactive);

    return 0;
}

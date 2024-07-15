#include "_priv.h"

const char *get_error_pos(const char *source, const char *err_pos, int *line) {
    *line = 1;
    const char *i = source;
    if (!i || *i == '\0') return source;
    const char *line_start = source;
    while (i != err_pos) {
        int eol = 0;
        if (i != err_pos && (*i == '\r' || *i == '\n')) {
            eol = 1;
            line_start = ++i;
        }

        if (eol)
            *line++;
        else
            i++;
    }
    return line_start;
}

static void print_error_line(const char *source, const char *err_pos) {
    if (err_pos == NULL) return;

    const char *i = err_pos;
    while (*i != '\0' && (*i != '\r' && *i != '\n')) fputc(*i++, stdout);
}

void print_error(const char *filename, const char *source, Capsule error) {
    int line = 0;
    const char *err_pos = CAPSULE_POSITION(error);
    if (err_pos == NULL) {
        fprintf(stdout, "%s: %s\n", filename, CapsuleError_str(error));
    } else {
        const char *line_start = get_error_pos(source, err_pos, &line);
        fprintf(stdout, "%s:%d\n", filename, line);
        if (*err_pos != '\0') {
            fprintf(stdout, "ERROR: %s\n | ", CapsuleError_str(error));
            print_error_line(source, line_start);
            fprintf(stdout, "\n  ");
            for (; line_start != err_pos; ++line_start) fputc(' ', stdout);
            fprintf(stdout, "^\n");
        } else {
            fprintf(stdout, "unexpected end of fle. %s line %d\n", CapsuleError_str(error), line);
        }
    }
}

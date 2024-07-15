#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "_priv.h"

static int lex(const char *str, const char **start, const char **end) {
    const char *ws = " \t\n";
    const char *delim = "(); \t\n";
    const char *prefix = "()\'`";

    str += strspn(str, ws);

    if (str[0] == '\0') {
        *start = *end = NULL;
        return -1;
    }

    *start = str;

    if (strchr(prefix, str[0]) != NULL)
        *end = str + 1;
    else if (str[0] == ',')
        *end = str + (str[1] == '@' ? 2 : 1);
    else if (str[0] == ';') {
        str = strchr(str, '\n');
        if (!str) {
            *start = *end = NULL;
            return CapsuleError_Syntax;
        }
        return lex(str, start, end);
    } else
        *end = str + strcspn(str, delim);

    return CapsuleError_None;
}

static Capsule parse_simple(const char *start, const char *end) {
    char *p = NULL;
    Capsule result;
    const char *position = start;
    long val = strtol(start, &p, 10);
    if (p == end) {
        Capsule value = CAPSULE_INT(val);
        CAPSULE_POSITION(value) = position;
        return value;
    }

    char *buffer = malloc(end - start + 1);
    p = buffer;
    while (start != end)
        *p++ = toupper(*start), ++start;
    *p = '\0';

    if (strcmp(buffer, "NIL") == 0) {
        result = Capsule_nil;
    } else {
        result = CAPSULE_SYMBOL(buffer);
    }

    free(buffer);
    CAPSULE_POSITION(result) = position;
    return result;
}

static Capsule read(const char *input, const char **end);

static Capsule read_list(const char *start, const char **end) {
    Capsule p;
    Capsule result;

    *end = start;
    p = result = Capsule_nil;

    for (;;) {
        const char *token;
        CapsuleError error;
        if ((error = lex(*end, &token, end)) != CapsuleError_None)
            return CAPSULE_ERROR_POS(error, start);

        if (token[0] == ')')
            return result;

        if (token[0] == '.' && *end - token == 1) {
            if (CAPSULE_NILP(p))
                return CAPSULE_ERROR_POS(CapsuleError_Syntax, token);

            Capsule item = read(*end, end);
            if (CAPSULE_ERRORP(item)) return item;

            CAPSULE_CDR(p) = item;

            if ((error = lex(*end, &token, end)) != CapsuleError_None)
                return CAPSULE_ERROR_POS(error, token);

            if (token[0] != ')')
                return CAPSULE_ERROR_POS(CapsuleError_Syntax, token);

            return result;
        }

        Capsule item = read(token, end);
        if (CAPSULE_ERRORP(item)) return item;

        if (CAPSULE_NILP(p)) {
            result = CAPSULE_CONS(item, Capsule_nil);
            p = result;
            CAPSULE_POSITION(result) = token;
        } else {
            CAPSULE_CDR(p) = CAPSULE_CONS(item, Capsule_nil);
            p = CAPSULE_CDR(p);
        }
    }
}

static Capsule read(const char *input, const char **end) {
    const char *token = NULL;

    CapsuleError err = lex(input, &token, end);
    if (err == EOF) return CAPSULE_ERROR(err);

    if (token[0] == '(') {
        return read_list(*end, end);
    } else if (token[0] == ')') {
        return CAPSULE_ERROR(CapsuleError_Syntax);
    } else if (token[0] == '\'') {
        Capsule result = CAPSULE_CONS(CAPSULE_SYMBOL("QUOTE"), CAPSULE_CONS(Capsule_nil, Capsule_nil));
        CAPSULE_CAR(CAPSULE_CDR(result)) = read(*end, end);
        CAPSULE_POSITION(result) = token;
        return result;
    } else if (token[0] == '`') {
        Capsule result = CAPSULE_CONS(CAPSULE_SYMBOL("QUASIQUOTE"), CAPSULE_CONS(Capsule_nil, Capsule_nil));
        CAPSULE_CAR(CAPSULE_CDR(result)) = read(*end, end);
        CAPSULE_POSITION(result) = token;
        return result;
    } else if (token[0] == ',') {
        Capsule result = CAPSULE_CONS(CAPSULE_SYMBOL(
                                          token[1] == '@' ? "UNQUOTE-SPLICING" : "UNQUOTE"),
                                      CAPSULE_CONS(Capsule_nil, Capsule_nil));
        CAPSULE_CAR(CAPSULE_CDR(result)) = read(*end, end);
        CAPSULE_POSITION(result) = token;
        return result;
    } else {
        return parse_simple(token, *end);
    }
}


Capsule Capsule_read(const char *source) {
    Capsule tree = CAPSULE_CONS(CAPSULE_SYMBOL("BEGIN"), Capsule_nil);
    Capsule i = tree;
    const char *p = source;

    for (;;) {
        Capsule item = read(p, &p);
        if (CAPSULE_ERRORP(item)) {
            if (CAPSULE_AS_ERROR(item) == EOF) break;
            return item;
        }

        CAPSULE_CDR(i) = CAPSULE_CONS(item, Capsule_nil);
        i = CAPSULE_CDR(i);
    }

    if (CAPSULE_NILP(CAPSULE_CDR(tree)))
        return Capsule_nil;
    else if (CAPSULE_NILP(CAPSULE_CDR(CAPSULE_CDR(tree))))
        return CAPSULE_CAR(CAPSULE_CDR(tree));
    return tree;
}
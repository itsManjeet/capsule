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

#include "capsule.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static CapsuleError read(const char* input, const char** end, Capsule* result);

static CapsuleError lex(const char* str, const char** start, const char** end) {
    const char* ws = " \t\n";
    const char* delim = "(){}[]; \t\n";
    const char* prefix = "(){}[]\'`";

    str += strspn(str, ws);

    if (str[0] == '\0') {
        *start = *end = NULL;
        return CAPSULE_ERROR_SYNTAX;
    }

    *start = str;

    if (strchr(prefix, str[0]) != NULL)
        *end = str + 1;
    else if (str[0] == ',')
        *end = str + (str[1] == '@' ? 2 : 1);
    else if (str[0] == '"') {
        *end = str + strcspn(str + 1, "\"") + 2;
    } else if (str[0] == ';') {
        str = strchr(str, '\n');
        if (!str) {
            *start = *end = NULL;
            return CAPSULE_ERROR_SYNTAX;
        }
        return lex(str, start, end);
    } else
        *end = str + strcspn(str, delim);

    return CAPSULE_ERROR_NONE;
}

static CapsuleError parse_simple(
        const char* start, const char* end, Capsule* result) {
    const char* iter = start;
    char* endptr;
    if (isdigit(*iter)) {
        int is_decimal = 0;
        while (isdigit(*iter)) *iter++;
        if (*iter == '.') {
            is_decimal = 1;
            iter++;
            while (isdigit(*iter)) *iter++;
        }

        if (is_decimal) {
            *result = CAPSULE_DECIMAL(strtod(start, &endptr));
        } else {
            *result = CAPSULE_INTEGER(strtol(start, &endptr, 10));
        }
        start = iter;
        return endptr == end ? CAPSULE_ERROR_NONE : CAPSULE_ERROR_SYNTAX;
    }

    char* buffer = malloc(end - start + 1);
    char* p = buffer;
    while (start != end) *p++ = toupper(*start), ++start;
    *p = '\0';

    if (strcmp(buffer, "NIL") == 0)
        *result = Capsule_nil;
    else
        *result = CAPSULE_SYMBOL(buffer);

    free(buffer);

    return CAPSULE_ERROR_NONE;
}

static CapsuleError read_string(
        const char* start, const char* end, Capsule* result) {
    size_t size = end - start - 2;
    char* buffer = malloc(sizeof(char) * (size + 1));
    for (int i = 1, j = 0; i <= size; ++i, ++j) {
        if (start[i] == '\\') {
            switch (start[++i]) {
            case 'n': buffer[j] = '\n'; break;
            case 't': buffer[j] = '\t'; break;
            case 'f': buffer[j] = '\f'; break;
            case 'b': buffer[j] = '\b'; break;
            case 'a': buffer[j] = '\a'; break;
            default: buffer[j] = start[i - 1]; break;
            }
        } else {
            buffer[j] = start[i];
        }
    }
    buffer[size] = '\0';
    *result = CAPSULE_STRING(buffer);
    return CAPSULE_ERROR_NONE;
}

static CapsuleError read_list(char list_start, char list_end, const char* start,
        const char** end, Capsule* result) {
    Capsule p;

    *end = start;
    p = *result = Capsule_nil;

    for (;;) {
        const char* token;
        Capsule item;
        CapsuleError err;

        err = lex(*end, &token, end);
        if (err) return err;

        if (token[0] == list_end) return CAPSULE_ERROR_NONE;

        if (token[0] == '.' && *end - token == 1) {

            if (CAPSULE_NILP(p)) return CAPSULE_ERROR_SYNTAX;

            err = read(*end, end, &item);
            if (err) return err;

            CAPSULE_CDR(p) = item;

            err = lex(*end, &token, end);
            if (!err && token[0] != list_end) err = CAPSULE_ERROR_SYNTAX;

            return err;
        }

        err = read(token, end, &item);
        if (err) return err;

        if (CAPSULE_NILP(p)) {

            *result = CAPSULE_CONS(item, Capsule_nil);
            p = *result;
        } else {
            CAPSULE_CDR(p) = CAPSULE_CONS(item, Capsule_nil);
            p = CAPSULE_CDR(p);
        }
    }
}

CapsuleError read(const char* input, const char** end, Capsule* result) {
    const char* token;
    CapsuleError err;

    err = lex(input, &token, end);
    if (err) return err;

    if (token[0] == '(') {
        return read_list('(', ')', *end, end, result);
    } else if (token[0] == '[') {
        return read_list('[', ']', *end, end, result);
    } else if (token[0] == '{') {
        return read_list('{', '}', *end, end, result);
    } else if (token[0] == ')' || token[0] == '}' || token[0] == ']') {
        return CAPSULE_ERROR_SYNTAX;
    } else if (token[0] == '\'') {
        *result = CAPSULE_CONS(CAPSULE_SYMBOL("QUOTE"),
                CAPSULE_CONS(Capsule_nil, Capsule_nil));
        return read(*end, end, &CAPSULE_CAR(CAPSULE_CDR(*result)));
    } else if (token[0] == '`') {
        *result = CAPSULE_CONS(CAPSULE_SYMBOL("QUASIQUOTE"),
                CAPSULE_CONS(Capsule_nil, Capsule_nil));
        return read(*end, end, &CAPSULE_CAR(CAPSULE_CDR(*result)));
    } else if (token[0] == ',') {
        *result =
                CAPSULE_CONS(CAPSULE_SYMBOL(token[1] == '@' ? "UNQUOTE-SPLICING"
                                                            : "UNQUOTE"),
                        CAPSULE_CONS(Capsule_nil, Capsule_nil));
        return read(*end, end, &CAPSULE_CAR(CAPSULE_CDR(*result)));
    } else if (token[0] == '"') {
        return read_string(token, *end, result);
    } else {
        return parse_simple(token, *end, result);
    }
}

CapsuleError Capsule_read(const char* source, Capsule* result) {
    const char* p = source;
    return read(p, &p, result);
}
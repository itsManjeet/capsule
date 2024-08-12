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
#include <stdio.h>

const char* Capsule_Error_str(CapsuleError error) {
    switch (error) {
    case CAPSULE_ERROR_NONE:
        return "";
    case CAPSULE_ERROR_ARGS:
        return "Invalid arguments";
    case CAPSULE_ERROR_TYPE:
        return "Invalid type";
    case CAPSULE_ERROR_SYNTAX:
        return "Invalid syntax";
    case CAPSULE_ERROR_UNBOUND:
        return "Unbounded value";
    case CAPSULE_ERROR_RUNTIME:
        return "Runtime Error";
    default:
        return "Unknown Error";
    }
}

void Capsule_print(Capsule atom, FILE* out) {
    switch (atom.type) {
    case CAPSULE_TYPE_NIL:
        fprintf(out, "NIL");
        break;
    case CAPSULE_TYPE_PAIR:
        fputc('(', out);
        Capsule_print(CAPSULE_CAR(atom), out);
        atom = CAPSULE_CDR(atom);
        while (!CAPSULE_NILP(atom)) {
            if (atom.type == CAPSULE_TYPE_PAIR) {
                fputc(' ', out);
                Capsule_print(CAPSULE_CAR(atom), out);
                atom = CAPSULE_CDR(atom);
            } else {
                fprintf(out, " . ");
                Capsule_print(atom, out);
                break;
            }
        }
        fputc(')', out);
        break;
    case CAPSULE_TYPE_STRING:
    case CAPSULE_TYPE_SYMBOL:
        fprintf(out, "%s", atom.as.symbol);
        break;
    case CAPSULE_TYPE_INTEGER:
        fprintf(out, "%ld", atom.as.integer);
        break;
    case CAPSULE_TYPE_DECIMAL:
        fprintf(out, "%lf", atom.as.decimal);
        break;
    case CAPSULE_TYPE_POINTER:
        fprintf(out, "%p", atom.as.pointer);
        break;
    case CAPSULE_TYPE_BUILTIN:
        fprintf(out, "#<BUILTIN:%p>", atom.as.builtin);
        break;
    case CAPSULE_TYPE_CLOSURE:
        fprintf(out, "#<CLOSURE:%p>", atom.as.pair);
        break;
    case CAPSULE_TYPE_MACRO:
        fprintf(out, "#<MACRO:%p>", atom.as.pair);
        break;
    }
}

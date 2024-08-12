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

#include "logo.h"
#include "priv.h"
#include <stdio.h>
#include <stdlib.h>

char* slurp(const char* path) {
    char* buffer = NULL;

    FILE* file = fopen(path, "rb");
    if (file == NULL) goto exit_return;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer = malloc(size + 1);
    if (buffer == NULL) goto exit_fclose;

    size_t count = fread(buffer, 1, size, file);
    buffer[count] = '\0';

exit_fclose:
    fclose(file);

exit_return:
    return buffer;
}

const char* Capsule_logo() { return LOGO; }
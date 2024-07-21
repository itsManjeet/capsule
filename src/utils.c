#include <stdlib.h>

#include "_priv.h"

char *readfile(const char *path) {
    char *buffer = NULL;

    FILE *file = fopen(path, "rb");
    if (!file)
        goto label_exit;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    buffer = malloc(size + 1);
    if (!buffer)
        goto label_close_file;

    size_t pos = fread(buffer, 1, size, file);
    if (pos == -1) {
        free(buffer);
        buffer = NULL;
    } else {
        buffer[pos] = '\0';
    }

    label_close_file:
    fclose(file);

    label_exit:
    return buffer;
}
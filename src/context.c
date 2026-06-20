#include "context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* copy_cstr(const char* value) {
  size_t length = strlen(value);
  char* copy = (char*)malloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, value, length + 1);
  return copy;
}

void srclang_vset_error(srclang_context_t ctx, const char* format, va_list args) {
  if (ctx == NULL) return;

  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) return;

  char* message = (char*)malloc((size_t)needed + 1);
  if (message == NULL) abort();
  vsnprintf(message, (size_t)needed + 1, format, args);

  free(ctx->error);
  ctx->error = message;
  ctx->had_error = true;
}

void srclang_set_error(srclang_context_t ctx, const char* format, ...) {
  va_list args;
  va_start(args, format);
  srclang_vset_error(ctx, format, args);
  va_end(args);
}

srclang_context_t srclang_context_new(void) {
  srclang_context_t ctx = (srclang_context_t)calloc(1, sizeof(srclang_context));
  if (ctx == NULL) abort();
  return ctx;
}

void srclang_context_clear_import_paths(srclang_context_t ctx) {
  if (ctx == NULL) return;
  for (int i = 0; i < ctx->import_path_count; i++) free(ctx->import_paths[i]);
  ctx->import_path_count = 0;
}

void srclang_context_free(srclang_context_t ctx) {
  if (ctx == NULL) return;
  srclang_context_clear_import_paths(ctx);
  free(ctx->import_paths);
  free(ctx->error);
  free(ctx);
}

void srclang_context_add_import_path(srclang_context_t ctx, const char* path) {
  if (ctx == NULL || path == NULL || path[0] == '\0') return;

  for (int i = 0; i < ctx->import_path_count; i++) {
    if (strcmp(ctx->import_paths[i], path) == 0) return;
  }

  if (ctx->import_path_capacity < ctx->import_path_count + 1) {
    int old_capacity = ctx->import_path_capacity;
    ctx->import_path_capacity = old_capacity < 4 ? 4 : old_capacity * 2;
    char** grown = (char**)realloc(ctx->import_paths, sizeof(char*) * (size_t)ctx->import_path_capacity);
    if (grown == NULL) abort();
    ctx->import_paths = grown;
  }

  ctx->import_paths[ctx->import_path_count++] = copy_cstr(path);
}

bool srclang_context_has_error(srclang_context_t ctx) {
  return ctx != NULL && ctx->had_error;
}

const char* srclang_context_last_error(srclang_context_t ctx) {
  if (ctx == NULL || ctx->error == NULL) return "";
  return ctx->error;
}

void srclang_context_clear_error(srclang_context_t ctx) {
  if (ctx == NULL) return;
  free(ctx->error);
  ctx->error = NULL;
  ctx->had_error = false;
}

char* srclang_read_file(srclang_context_t ctx, const char* filepath) {
  FILE* file = fopen(filepath, "rb");
  if (file == NULL) {
    srclang_set_error(ctx, "Could not open '%s'.", filepath);
    return NULL;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    srclang_set_error(ctx, "Could not seek '%s'.", filepath);
    return NULL;
  }

  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    srclang_set_error(ctx, "Could not read size for '%s'.", filepath);
    return NULL;
  }
  rewind(file);

  char* buffer = (char*)malloc((size_t)size + 1);
  if (buffer == NULL) abort();
  size_t read = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read] = '\0';
  return buffer;
}

#ifndef SRCLANG_CONTEXT_H
#define SRCLANG_CONTEXT_H

#include "common.h"

#include <stdarg.h>

typedef struct srclang_context* srclang_context_t;

typedef struct srclang_context {
  char* error;
  bool had_error;

  char** import_paths;
  int import_path_count;
  int import_path_capacity;
} srclang_context;

srclang_context_t srclang_context_new(void);
void srclang_context_free(srclang_context_t ctx);

void srclang_context_add_import_path(srclang_context_t ctx, const char* path);
void srclang_context_clear_import_paths(srclang_context_t ctx);

bool srclang_context_has_error(srclang_context_t ctx);
const char* srclang_context_last_error(srclang_context_t ctx);
void srclang_context_clear_error(srclang_context_t ctx);

void srclang_set_error(srclang_context_t ctx, const char* format, ...);
void srclang_vset_error(srclang_context_t ctx, const char* format, va_list args);
char* srclang_read_file(srclang_context_t ctx, const char* filepath);

#endif

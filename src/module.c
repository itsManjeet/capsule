#include "module.h"

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SRCLANG_PATH_SEPARATOR ';'
#else
#define SRCLANG_PATH_SEPARATOR ':'
#endif

typedef struct {
  char** values;
  int count;
  int capacity;
} srclang_name_set_t;

static void set_init(srclang_name_set_t* set) {
  set->values = NULL;
  set->count = 0;
  set->capacity = 0;
}

static void set_free(srclang_name_set_t* set) {
  for (int i = 0; i < set->count; i++) free(set->values[i]);
  free(set->values);
  set_init(set);
}

static bool set_contains(const srclang_name_set_t* set, const char* value) {
  for (int i = 0; i < set->count; i++) {
    if (strcmp(set->values[i], value) == 0) return true;
  }
  return false;
}

static bool set_add(srclang_name_set_t* set, const char* value) {
  if (value == NULL || set_contains(set, value)) return false;
  if (set->capacity < set->count + 1) {
    int old_capacity = set->capacity;
    set->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    char** grown = (char**)realloc(set->values, sizeof(char*) * (size_t)set->capacity);
    if (grown == NULL) abort();
    set->values = grown;
  }
  set->values[set->count++] = srclang_ast_copy(value, (int)strlen(value));
  return true;
}

static void set_truncate(srclang_name_set_t* set, int count) {
  while (set->count > count) free(set->values[--set->count]);
}

static const char* declaration_name(const srclang_stmt_t* stmt) {
  if (stmt == NULL) return NULL;
  switch (stmt->kind) {
    case STMT_VAR: return stmt->as.var.name;
    case STMT_FUNCTION: return stmt->as.function.name;
    case STMT_CLASS: return stmt->as.class_decl.name;
    default: return NULL;
  }
}

static void collect_expr_refs(const srclang_expr_t* expr, srclang_name_set_t* defined, srclang_name_set_t* refs);
static void collect_stmt_refs(const srclang_stmt_t* stmt, srclang_name_set_t* defined, srclang_name_set_t* refs);

static void collect_function_refs(
    const srclang_function_decl_t* function,
    srclang_name_set_t* defined,
    srclang_name_set_t* refs,
    bool bind_name) {
  int old_count = defined->count;
  if (bind_name) set_add(defined, function->name);
  for (int i = 0; i < function->param_count; i++) set_add(defined, function->params[i].name);
  if (function->body != NULL) collect_stmt_refs(function->body, defined, refs);
  set_truncate(defined, old_count);
}

static void collect_expr_refs(const srclang_expr_t* expr, srclang_name_set_t* defined, srclang_name_set_t* refs) {
  if (expr == NULL) return;
  switch (expr->kind) {
    case EXPR_LITERAL:
      break;
    case EXPR_VARIABLE:
      if (!set_contains(defined, expr->as.variable)) set_add(refs, expr->as.variable);
      break;
    case EXPR_ASSIGN:
      if (!set_contains(defined, expr->as.assign.name)) set_add(refs, expr->as.assign.name);
      collect_expr_refs(expr->as.assign.value, defined, refs);
      break;
    case EXPR_BINARY:
      collect_expr_refs(expr->as.binary.left, defined, refs);
      collect_expr_refs(expr->as.binary.right, defined, refs);
      break;
    case EXPR_LOGICAL:
      collect_expr_refs(expr->as.logical.left, defined, refs);
      collect_expr_refs(expr->as.logical.right, defined, refs);
      break;
    case EXPR_UNARY:
      collect_expr_refs(expr->as.unary.right, defined, refs);
      break;
    case EXPR_CALL:
      collect_expr_refs(expr->as.call.callee, defined, refs);
      for (int i = 0; i < expr->as.call.arg_count; i++) {
        collect_expr_refs(expr->as.call.args[i], defined, refs);
      }
      break;
    case EXPR_GET:
      collect_expr_refs(expr->as.get.object, defined, refs);
      break;
    case EXPR_SET:
      collect_expr_refs(expr->as.set.object, defined, refs);
      collect_expr_refs(expr->as.set.value, defined, refs);
      break;
    case EXPR_THIS:
    case EXPR_SUPER:
      break;
    case EXPR_GROUPING:
      collect_expr_refs(expr->as.grouping, defined, refs);
      break;
  }
}

static void collect_class_refs(const srclang_stmt_t* stmt, srclang_name_set_t* defined, srclang_name_set_t* refs) {
  int old_count = defined->count;
  set_add(defined, stmt->as.class_decl.name);
  if (stmt->as.class_decl.superclass != NULL && !set_contains(defined, stmt->as.class_decl.superclass)) {
    set_add(refs, stmt->as.class_decl.superclass);
  }

  for (int i = 0; i < stmt->as.class_decl.member_count; i++) {
    srclang_class_member_t* member = &stmt->as.class_decl.members[i];
    int member_scope = defined->count;
    set_add(defined, "this");
    set_add(defined, "super");
    if (member->kind == CLASS_MEMBER_FIELD) {
      collect_expr_refs(member->as.field.initializer, defined, refs);
    } else {
      collect_function_refs(&member->as.method.function, defined, refs, false);
    }
    set_truncate(defined, member_scope);
  }

  set_truncate(defined, old_count);
}

static void collect_stmt_refs(const srclang_stmt_t* stmt, srclang_name_set_t* defined, srclang_name_set_t* refs) {
  if (stmt == NULL) return;
  switch (stmt->kind) {
    case STMT_EXPR:
      collect_expr_refs(stmt->as.expression, defined, refs);
      break;
    case STMT_VAR:
      collect_expr_refs(stmt->as.var.initializer, defined, refs);
      set_add(defined, stmt->as.var.name);
      break;
    case STMT_BLOCK: {
      int old_count = defined->count;
      for (int i = 0; i < stmt->as.block.count; i++) collect_stmt_refs(stmt->as.block.declarations[i], defined, refs);
      set_truncate(defined, old_count);
      break;
    }
    case STMT_IF:
      collect_expr_refs(stmt->as.if_stmt.condition, defined, refs);
      collect_stmt_refs(stmt->as.if_stmt.then_branch, defined, refs);
      collect_stmt_refs(stmt->as.if_stmt.else_branch, defined, refs);
      break;
    case STMT_WHILE:
      collect_expr_refs(stmt->as.while_stmt.condition, defined, refs);
      collect_stmt_refs(stmt->as.while_stmt.body, defined, refs);
      break;
    case STMT_FOR: {
      int old_count = defined->count;
      collect_stmt_refs(stmt->as.for_stmt.initializer, defined, refs);
      collect_expr_refs(stmt->as.for_stmt.condition, defined, refs);
      collect_expr_refs(stmt->as.for_stmt.increment, defined, refs);
      collect_stmt_refs(stmt->as.for_stmt.body, defined, refs);
      set_truncate(defined, old_count);
      break;
    }
    case STMT_RETURN:
      collect_expr_refs(stmt->as.return_value, defined, refs);
      break;
    case STMT_FUNCTION:
      collect_function_refs(&stmt->as.function, defined, refs, true);
      break;
    case STMT_CLASS:
      collect_class_refs(stmt, defined, refs);
      break;
    case STMT_IMPORT:
      break;
  }
}

static void collect_program_refs(const srclang_ast_program_t* program, srclang_name_set_t* refs) {
  srclang_name_set_t defined;
  set_init(&defined);
  for (int i = 0; i < program->count; i++) {
    const char* name = declaration_name(program->declarations[i]);
    if (name != NULL) set_add(&defined, name);
  }
  for (int i = 0; i < program->count; i++) {
    if (program->declarations[i] != NULL && program->declarations[i]->kind != STMT_IMPORT) {
      collect_stmt_refs(program->declarations[i], &defined, refs);
    }
  }
  set_free(&defined);
}

static void collect_declaration_refs(const srclang_stmt_t* stmt, srclang_name_set_t* refs) {
  srclang_name_set_t defined;
  set_init(&defined);
  collect_stmt_refs(stmt, &defined, refs);
  set_free(&defined);
}

static char* dirname_for_path(const char* path) {
  if (path == NULL || path[0] == '\0') return srclang_ast_copy(".", 1);
  const char* slash = strrchr(path, '/');
  if (slash == NULL) return srclang_ast_copy(".", 1);
  if (slash == path) return srclang_ast_copy("/", 1);
  return srclang_ast_copy(path, (int)(slash - path));
}

static char* module_relative_path(const char* base_dir, const char* import_path) {
  size_t base_length = strlen(base_dir);
  size_t import_length = strlen(import_path);
  size_t length = base_length + 1 + import_length + 4 + 1;
  char* path = (char*)malloc(length);
  if (path == NULL) abort();
  memcpy(path, base_dir, base_length);
  path[base_length] = '/';
  for (size_t i = 0; i < import_length; i++) {
    path[base_length + 1 + i] = import_path[i] == '.' ? '/' : import_path[i];
  }
  memcpy(path + base_length + 1 + import_length, ".src", 5);
  return path;
}

static bool try_module_root(
    srclang_context_t ctx,
    const char* root,
    const char* import_path,
    char** source,
    char** resolved_path);

static char* read_file(srclang_context_t ctx, const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) return NULL;
  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    srclang_set_error(ctx, "Could not seek imported module '%s'.", path);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    srclang_set_error(ctx, "Could not read imported module '%s'.", path);
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

static bool try_module_root(
    srclang_context_t ctx,
    const char* root,
    const char* import_path,
    char** source,
    char** resolved_path) {
  if (root == NULL || root[0] == '\0') return false;
  char* path = module_relative_path(root, import_path);
  char* contents = read_file(ctx, path);
  if (contents == NULL) {
    free(path);
    return false;
  }
  *source = contents;
  *resolved_path = path;
  return true;
}

static bool try_env_import_paths(
    srclang_context_t ctx,
    const char* import_path,
    char** source,
    char** resolved_path) {
  const char* env = getenv("SRCLANG_PATH");
  if (env == NULL || env[0] == '\0') return false;

  const char* segment = env;
  while (*segment != '\0') {
    const char* end = strchr(segment, SRCLANG_PATH_SEPARATOR);
    size_t length = end == NULL ? strlen(segment) : (size_t)(end - segment);
    if (length > 0) {
      char* root = srclang_ast_copy(segment, (int)length);
      bool found = try_module_root(ctx, root, import_path, source, resolved_path);
      free(root);
      if (found) return true;
    }
    if (end == NULL) break;
    segment = end + 1;
  }

  return false;
}

static bool resolve_module_path(
    srclang_context_t ctx,
    const char* import_path,
    const char* origin_path,
    char** source,
    char** resolved_path) {
  *source = NULL;
  *resolved_path = NULL;

  char* base_dir = dirname_for_path(origin_path);
  bool found = try_module_root(ctx, base_dir, import_path, source, resolved_path);
  free(base_dir);
  if (found) return true;

  for (int i = 0; i < ctx->import_path_count; i++) {
    if (try_module_root(ctx, ctx->import_paths[i], import_path, source, resolved_path)) return true;
  }

  if (try_env_import_paths(ctx, import_path, source, resolved_path)) return true;

  return try_module_root(ctx, ".", import_path, source, resolved_path);
}

static bool resolve_program(
    srclang_context_t ctx,
    srclang_ast_program_t* input,
    const char* origin_path,
    srclang_ast_program_t* output,
    srclang_name_set_t* import_stack);

static bool resolve_module(
    srclang_context_t ctx,
    const char* import_path,
    const char* origin_path,
    srclang_ast_program_t* output,
    srclang_name_set_t* import_stack) {
  srclang_ast_program_init(output);

  if (set_contains(import_stack, import_path)) {
    srclang_set_error(ctx, "Cyclic import involving '%s'.", import_path);
    return false;
  }

  char* path = NULL;
  char* source = NULL;
  bool resolved = resolve_module_path(ctx, import_path, origin_path, &source, &path);

  if (!resolved) {
    srclang_set_error(
        ctx,
        "Could not resolve import '%s' from '%s'. Searched importer directory, context import paths, SRCLANG_PATH, and current directory.",
        import_path,
        origin_path == NULL ? "." : origin_path);
    free(path);
    return false;
  }

  set_add(import_stack, import_path);

  srclang_ast_program_t parsed;
  if (!srclang_parse_source(ctx, source, &parsed)) {
    free(source);
    free(path);
    set_truncate(import_stack, import_stack->count - 1);
    return false;
  }

  bool ok = resolve_program(ctx, &parsed, path, output, import_stack);
  srclang_ast_program_free(&parsed);
  free(source);
  free(path);
  set_truncate(import_stack, import_stack->count - 1);
  return ok;
}

static void build_symbol_set(const srclang_ast_program_t* program, srclang_name_set_t* symbols) {
  for (int i = 0; i < program->count; i++) {
    const char* name = declaration_name(program->declarations[i]);
    if (name != NULL) set_add(symbols, name);
  }
}

static void mark_needed_from_refs(
    const srclang_name_set_t* refs,
    const srclang_name_set_t* symbols,
    srclang_name_set_t* needed) {
  for (int i = 0; i < refs->count; i++) {
    if (set_contains(symbols, refs->values[i])) set_add(needed, refs->values[i]);
  }
}

static bool expand_needed_declarations(const srclang_ast_program_t* module, const srclang_name_set_t* symbols, srclang_name_set_t* needed) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (int i = 0; i < module->count; i++) {
      const char* name = declaration_name(module->declarations[i]);
      if (name == NULL || !set_contains(needed, name)) continue;

      srclang_name_set_t refs;
      set_init(&refs);
      collect_declaration_refs(module->declarations[i], &refs);
      int before = needed->count;
      mark_needed_from_refs(&refs, symbols, needed);
      if (needed->count != before) changed = true;
      set_free(&refs);
    }
  }
  return true;
}

static bool resolve_program(
    srclang_context_t ctx,
    srclang_ast_program_t* input,
    const char* origin_path,
    srclang_ast_program_t* output,
    srclang_name_set_t* import_stack) {
  srclang_name_set_t refs;
  set_init(&refs);
  collect_program_refs(input, &refs);

  for (int i = 0; i < input->count; i++) {
    srclang_stmt_t* stmt = input->declarations[i];
    if (stmt == NULL || stmt->kind != STMT_IMPORT) continue;

    srclang_ast_program_t module;
    if (!resolve_module(ctx, stmt->as.import_path, origin_path, &module, import_stack)) {
      set_free(&refs);
      return false;
    }

    srclang_name_set_t symbols;
    srclang_name_set_t needed;
    set_init(&symbols);
    set_init(&needed);
    build_symbol_set(&module, &symbols);
    mark_needed_from_refs(&refs, &symbols, &needed);
    expand_needed_declarations(&module, &symbols, &needed);

    for (int j = 0; j < module.count; j++) {
      const char* name = declaration_name(module.declarations[j]);
      if (name != NULL && set_contains(&needed, name)) {
        srclang_ast_program_write(output, module.declarations[j]);
        module.declarations[j] = NULL;
      }
    }

    set_free(&symbols);
    set_free(&needed);
    srclang_ast_program_free(&module);
  }

  for (int i = 0; i < input->count; i++) {
    srclang_stmt_t* stmt = input->declarations[i];
    if (stmt == NULL || stmt->kind == STMT_IMPORT) continue;
    srclang_ast_program_write(output, stmt);
    input->declarations[i] = NULL;
  }

  set_free(&refs);
  return true;
}

bool srclang_parse_source_with_imports(
    srclang_context_t ctx,
    const char* source,
    const char* origin_path,
    srclang_ast_program_t* program) {
  srclang_ast_program_init(program);
  srclang_ast_program_t parsed;
  if (!srclang_parse_source(ctx, source, &parsed)) return false;

  srclang_name_set_t import_stack;
  set_init(&import_stack);
  bool ok = resolve_program(ctx, &parsed, origin_path, program, &import_stack);
  set_free(&import_stack);
  srclang_ast_program_free(&parsed);

  if (!ok) {
    srclang_ast_program_free(program);
    return false;
  }
  return true;
}

#include "native_bytecode.h"

#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char* data;
  size_t count;
  size_t capacity;
} srclang_text_writer_t;

static char* copy_text(const char* text) {
  size_t length = strlen(text);
  char* copy = (char*)malloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, text, length + 1);
  return copy;
}

static void writer_init(srclang_text_writer_t* writer) {
  writer->data = NULL;
  writer->count = 0;
  writer->capacity = 0;
}

static void writer_reserve(srclang_text_writer_t* writer, size_t additional) {
  if (writer->capacity >= writer->count + additional + 1) return;
  size_t capacity = writer->capacity < 512 ? 512 : writer->capacity * 2;
  while (capacity < writer->count + additional + 1) capacity *= 2;
  char* grown = (char*)realloc(writer->data, capacity);
  if (grown == NULL) abort();
  writer->data = grown;
  writer->capacity = capacity;
}

static void writer_append(srclang_text_writer_t* writer, const char* text) {
  size_t length = strlen(text);
  writer_reserve(writer, length);
  memcpy(writer->data + writer->count, text, length);
  writer->count += length;
  writer->data[writer->count] = '\0';
}

static void writer_appendf(srclang_text_writer_t* writer, const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return;
  }
  writer_reserve(writer, (size_t)needed);
  vsnprintf(writer->data + writer->count, writer->capacity - writer->count, format, args);
  writer->count += (size_t)needed;
  va_end(args);
}

char* srclang_native_mangle(const char* prefix, const char* name) {
  size_t prefix_len = strlen(prefix);
  size_t name_len = strlen(name);
  char* out = (char*)malloc(prefix_len + name_len + 1);
  if (out == NULL) abort();
  memcpy(out, prefix, prefix_len);
  for (size_t i = 0; i < name_len; i++) {
    char c = name[i];
    out[prefix_len + i] = ((c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9')) ? c : '_';
  }
  out[prefix_len + name_len] = '\0';
  return out;
}

const char* srclang_native_type_name(srclang_native_type_t type) {
  switch (type) {
    case SRCLANG_NATIVE_TYPE_NONE: return "none";
    case SRCLANG_NATIVE_TYPE_NIL: return "nil";
    case SRCLANG_NATIVE_TYPE_BOOL: return "bool";
    case SRCLANG_NATIVE_TYPE_U8: return "u8";
    case SRCLANG_NATIVE_TYPE_U16: return "u16";
    case SRCLANG_NATIVE_TYPE_U32: return "u32";
    case SRCLANG_NATIVE_TYPE_U64: return "u64";
    case SRCLANG_NATIVE_TYPE_I8: return "i8";
    case SRCLANG_NATIVE_TYPE_I16: return "i16";
    case SRCLANG_NATIVE_TYPE_I32: return "i32";
    case SRCLANG_NATIVE_TYPE_I64: return "i64";
    case SRCLANG_NATIVE_TYPE_F32: return "f32";
    case SRCLANG_NATIVE_TYPE_F64: return "f64";
    case SRCLANG_NATIVE_TYPE_STR: return "str";
    case SRCLANG_NATIVE_TYPE_STRING: return "string";
    case SRCLANG_NATIVE_TYPE_PTR: return "ptr";
    case SRCLANG_NATIVE_TYPE_ANY: return "any";
  }
  return "unknown";
}

static srclang_native_type_t type_from_annotation(const srclang_type_node_t* type) {
  if (type == NULL) return SRCLANG_NATIVE_TYPE_I64;
  if (type->kind != TYPE_BUILTIN) return SRCLANG_NATIVE_TYPE_PTR;
  switch (type->as.builtin) {
    case BUILTIN_ANY: return SRCLANG_NATIVE_TYPE_ANY;
    case BUILTIN_NIL: return SRCLANG_NATIVE_TYPE_NIL;
    case BUILTIN_BOOL: return SRCLANG_NATIVE_TYPE_BOOL;
    case BUILTIN_NUM: return SRCLANG_NATIVE_TYPE_I64;
    case BUILTIN_U8: return SRCLANG_NATIVE_TYPE_U8;
    case BUILTIN_U16: return SRCLANG_NATIVE_TYPE_U16;
    case BUILTIN_U32: return SRCLANG_NATIVE_TYPE_U32;
    case BUILTIN_U64: return SRCLANG_NATIVE_TYPE_U64;
    case BUILTIN_I8: return SRCLANG_NATIVE_TYPE_I8;
    case BUILTIN_I16: return SRCLANG_NATIVE_TYPE_I16;
    case BUILTIN_I32: return SRCLANG_NATIVE_TYPE_I32;
    case BUILTIN_I64: return SRCLANG_NATIVE_TYPE_I64;
    case BUILTIN_F32: return SRCLANG_NATIVE_TYPE_F32;
    case BUILTIN_F64: return SRCLANG_NATIVE_TYPE_F64;
    case BUILTIN_STR: return SRCLANG_NATIVE_TYPE_STR;
    case BUILTIN_STRING: return SRCLANG_NATIVE_TYPE_STRING;
    case BUILTIN_PTR: return SRCLANG_NATIVE_TYPE_PTR;
    case BUILTIN_NONE: return SRCLANG_NATIVE_TYPE_NONE;
  }
  return SRCLANG_NATIVE_TYPE_I64;
}

void srclang_native_program_init(srclang_native_program_t* program, const char* module_name) {
  program->module_name = copy_text(module_name != NULL ? module_name : "main");
  program->globals = NULL;
  program->global_count = 0;
  program->global_capacity = 0;
  program->functions = NULL;
  program->function_count = 0;
  program->function_capacity = 0;
  program->strings = NULL;
  program->string_count = 0;
  program->string_capacity = 0;
}

static void function_free(srclang_native_function_t* function) {
  free(function->name);
  free(function->symbol);
  for (int i = 0; i < function->local_count; i++) free(function->locals[i].name);
  free(function->locals);
  for (int i = 0; i < function->code_count; i++) free(function->code[i].symbol);
  free(function->code);
}

void srclang_native_program_free(srclang_native_program_t* program) {
  free(program->module_name);
  for (int i = 0; i < program->global_count; i++) free(program->globals[i].name);
  free(program->globals);
  for (int i = 0; i < program->function_count; i++) function_free(&program->functions[i]);
  free(program->functions);
  for (int i = 0; i < program->string_count; i++) {
    free(program->strings[i].value);
    free(program->strings[i].label);
  }
  free(program->strings);
  memset(program, 0, sizeof(*program));
}

static int program_add_string(srclang_native_program_t* program, const char* value) {
  if (program->string_capacity < program->string_count + 1) {
    int old_capacity = program->string_capacity;
    program->string_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    srclang_native_string_t* grown = (srclang_native_string_t*)realloc(
        program->strings,
        sizeof(srclang_native_string_t) * (size_t)program->string_capacity);
    if (grown == NULL) abort();
    program->strings = grown;
  }
  int index = program->string_count++;
  program->strings[index].value = copy_text(value);
  char label[64];
  snprintf(label, sizeof(label), ".Lsrclang_str_%d", index);
  program->strings[index].label = copy_text(label);
  return index;
}

static int program_find_global(const srclang_native_program_t* program, const char* name) {
  for (int i = 0; i < program->global_count; i++) {
    if (strcmp(program->globals[i].name, name) == 0) return i;
  }
  return -1;
}

static srclang_native_function_t* program_find_function(srclang_native_program_t* program, const char* name) {
  for (int i = 0; i < program->function_count; i++) {
    if (strcmp(program->functions[i].name, name) == 0) return &program->functions[i];
  }
  return NULL;
}

static void program_add_global(srclang_native_program_t* program, const char* name, srclang_native_type_t type) {
  if (program_find_global(program, name) >= 0) return;
  if (program->global_capacity < program->global_count + 1) {
    int old_capacity = program->global_capacity;
    program->global_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    srclang_native_global_t* grown = (srclang_native_global_t*)realloc(
        program->globals,
        sizeof(srclang_native_global_t) * (size_t)program->global_capacity);
    if (grown == NULL) abort();
    program->globals = grown;
  }
  srclang_native_global_t* global = &program->globals[program->global_count];
  memset(global, 0, sizeof(*global));
  global->name = copy_text(name);
  global->type = type;
  program->global_count++;
}

static bool program_set_static_global_initializer(
    srclang_context_t ctx,
    srclang_native_program_t* program,
    const srclang_stmt_t* stmt) {
  if (stmt->as.var.initializer == NULL) return true;

  int index = program_find_global(program, stmt->as.var.name);
  if (index < 0) return true;

  const srclang_expr_t* initializer = stmt->as.var.initializer;
  if (initializer->kind != EXPR_LITERAL) {
    srclang_set_error(
        ctx,
        "[line %d] Library/archive global initializer for '%s' must be a literal.",
        stmt->line,
        stmt->as.var.name);
    return false;
  }

  srclang_native_global_t* global = &program->globals[index];
  global->has_initializer = true;
  switch (initializer->as.literal.kind) {
    case LITERAL_NUMBER:
      global->initializer_value = (long long)initializer->as.literal.as.number;
      break;
    case LITERAL_BOOL:
      global->initializer_value = initializer->as.literal.as.boolean ? 1 : 0;
      break;
    case LITERAL_NIL:
      global->initializer_value = 0;
      break;
    case LITERAL_STRING:
      global->initializer_is_string = true;
      global->initializer_string = program_add_string(program, initializer->as.literal.as.string);
      break;
  }

  return true;
}

static srclang_native_function_t* program_add_function(srclang_native_program_t* program, const char* name, bool is_external) {
  if (program->function_capacity < program->function_count + 1) {
    int old_capacity = program->function_capacity;
    program->function_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    srclang_native_function_t* grown = (srclang_native_function_t*)realloc(
        program->functions,
        sizeof(srclang_native_function_t) * (size_t)program->function_capacity);
    if (grown == NULL) abort();
    program->functions = grown;
  }
  srclang_native_function_t* function = &program->functions[program->function_count++];
  memset(function, 0, sizeof(*function));
  function->name = copy_text(name);
  function->symbol = is_external
      ? copy_text(name)
      : (strcmp(name, "main") == 0 ? copy_text("main") : srclang_native_mangle("srclang_sym_", name));
  function->return_type = SRCLANG_NATIVE_TYPE_I64;
  function->is_external = is_external;
  return function;
}

static int function_add_local(srclang_native_function_t* function, const char* name, srclang_native_type_t type) {
  for (int i = 0; i < function->local_count; i++) {
    if (strcmp(function->locals[i].name, name) == 0) return function->locals[i].slot;
  }
  if (function->local_capacity < function->local_count + 1) {
    int old_capacity = function->local_capacity;
    function->local_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    srclang_native_local_t* grown = (srclang_native_local_t*)realloc(
        function->locals,
        sizeof(srclang_native_local_t) * (size_t)function->local_capacity);
    if (grown == NULL) abort();
    function->locals = grown;
  }
  int slot = function->local_count;
  function->locals[function->local_count].name = copy_text(name);
  function->locals[function->local_count].type = type;
  function->locals[function->local_count].slot = slot;
  function->local_count++;
  return slot;
}

static int function_find_local(const srclang_native_function_t* function, const char* name) {
  for (int i = function->local_count - 1; i >= 0; i--) {
    if (strcmp(function->locals[i].name, name) == 0) return function->locals[i].slot;
  }
  return -1;
}

static int emit(srclang_native_function_t* function, srclang_native_opcode_t op, int line) {
  if (function->code_capacity < function->code_count + 1) {
    int old_capacity = function->code_capacity;
    function->code_capacity = old_capacity < 16 ? 16 : old_capacity * 2;
    srclang_native_instr_t* grown = (srclang_native_instr_t*)realloc(
        function->code,
        sizeof(srclang_native_instr_t) * (size_t)function->code_capacity);
    if (grown == NULL) abort();
    function->code = grown;
  }
  int index = function->code_count++;
  memset(&function->code[index], 0, sizeof(function->code[index]));
  function->code[index].op = op;
  function->code[index].line = line;
  return index;
}

static void patch_target(srclang_native_function_t* function, int instruction, int target) {
  function->code[instruction].operand = target;
}

typedef struct {
  srclang_context_t ctx;
  srclang_native_program_t* program;
  srclang_native_function_t* function;
} srclang_native_compiler_t;

static bool compile_stmt(srclang_native_compiler_t* compiler, const srclang_stmt_t* stmt);
static bool compile_expr(srclang_native_compiler_t* compiler, const srclang_expr_t* expr);

static bool is_name(const char* value, const char* expected) {
  return value != NULL && strcmp(value, expected) == 0;
}

static bool is_inline_asm_call(const srclang_expr_t* expr) {
  return expr != NULL &&
         expr->kind == EXPR_CALL &&
         expr->as.call.callee->kind == EXPR_VARIABLE &&
         is_name(expr->as.call.callee->as.variable, "asm");
}

static bool emit_inline_asm(srclang_native_compiler_t* compiler, const srclang_expr_t* expr) {
  if (!is_inline_asm_call(expr)) return false;
  if (expr->as.call.arg_count != 1 ||
      expr->as.call.args[0]->kind != EXPR_LITERAL ||
      expr->as.call.args[0]->as.literal.kind != LITERAL_STRING) {
    srclang_set_error(compiler->ctx, "[line %d] asm(...) expects exactly one string literal.", expr->line);
    return false;
  }

  int ins = emit(compiler->function, SRCLANG_BC_INLINE_ASM, expr->line);
  compiler->function->code[ins].symbol = copy_text(expr->as.call.args[0]->as.literal.as.string);
  return true;
}

static bool compile_variable_load(srclang_native_compiler_t* compiler, const char* name, int line) {
  int slot = function_find_local(compiler->function, name);
  if (slot >= 0) {
    int ins = emit(compiler->function, SRCLANG_BC_LOAD_LOCAL, line);
    compiler->function->code[ins].operand = slot;
    return true;
  }
  if (program_find_global(compiler->program, name) >= 0) {
    int ins = emit(compiler->function, SRCLANG_BC_LOAD_GLOBAL, line);
    compiler->function->code[ins].symbol = srclang_native_mangle("srclang_global_", name);
    return true;
  }
  srclang_set_error(compiler->ctx, "[line %d] Undefined symbol '%s'.", line, name);
  return false;
}

static bool compile_expr(srclang_native_compiler_t* compiler, const srclang_expr_t* expr) {
  switch (expr->kind) {
    case EXPR_LITERAL:
      if (expr->as.literal.kind == LITERAL_NUMBER) {
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, expr->line);
        compiler->function->code[ins].imm = (long long)expr->as.literal.as.number;
        return true;
      }
      if (expr->as.literal.kind == LITERAL_BOOL) {
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, expr->line);
        compiler->function->code[ins].imm = expr->as.literal.as.boolean ? 1 : 0;
        return true;
      }
      if (expr->as.literal.kind == LITERAL_NIL) {
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, expr->line);
        compiler->function->code[ins].imm = 0;
        return true;
      }
      if (expr->as.literal.kind == LITERAL_STRING) {
        int index = program_add_string(compiler->program, expr->as.literal.as.string);
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_STR, expr->line);
        compiler->function->code[ins].operand = index;
        return true;
      }
      break;
    case EXPR_VARIABLE:
      return compile_variable_load(compiler, expr->as.variable, expr->line);
    case EXPR_ASSIGN: {
      if (!compile_expr(compiler, expr->as.assign.value)) return false;
      int slot = function_find_local(compiler->function, expr->as.assign.name);
      if (slot >= 0) {
        int ins = emit(compiler->function, SRCLANG_BC_STORE_LOCAL, expr->line);
        compiler->function->code[ins].operand = slot;
        return true;
      }
      if (program_find_global(compiler->program, expr->as.assign.name) >= 0) {
        int ins = emit(compiler->function, SRCLANG_BC_STORE_GLOBAL, expr->line);
        compiler->function->code[ins].symbol = srclang_native_mangle("srclang_global_", expr->as.assign.name);
        return true;
      }
      srclang_set_error(compiler->ctx, "[line %d] Undefined assignment target '%s'.", expr->line, expr->as.assign.name);
      return false;
    }
    case EXPR_GROUPING:
      return compile_expr(compiler, expr->as.grouping);
    case EXPR_UNARY:
      if (!compile_expr(compiler, expr->as.unary.right)) return false;
      emit(compiler->function, expr->as.unary.op == TOKEN_MINUS ? SRCLANG_BC_NEG_I64 : SRCLANG_BC_NOT, expr->line);
      return true;
    case EXPR_BINARY:
      if (!compile_expr(compiler, expr->as.binary.left) || !compile_expr(compiler, expr->as.binary.right)) return false;
      switch (expr->as.binary.op) {
        case TOKEN_PLUS: emit(compiler->function, SRCLANG_BC_ADD_I64, expr->line); break;
        case TOKEN_MINUS: emit(compiler->function, SRCLANG_BC_SUB_I64, expr->line); break;
        case TOKEN_STAR: emit(compiler->function, SRCLANG_BC_MUL_I64, expr->line); break;
        case TOKEN_SLASH: emit(compiler->function, SRCLANG_BC_DIV_I64, expr->line); break;
        case TOKEN_PERCENT: emit(compiler->function, SRCLANG_BC_MOD_I64, expr->line); break;
        case TOKEN_EQUAL_EQUAL: emit(compiler->function, SRCLANG_BC_EQ_I64, expr->line); break;
        case TOKEN_BANG_EQUAL: emit(compiler->function, SRCLANG_BC_NE_I64, expr->line); break;
        case TOKEN_GREATER: emit(compiler->function, SRCLANG_BC_GT_I64, expr->line); break;
        case TOKEN_GREATER_EQUAL: emit(compiler->function, SRCLANG_BC_GE_I64, expr->line); break;
        case TOKEN_LESS: emit(compiler->function, SRCLANG_BC_LT_I64, expr->line); break;
        case TOKEN_LESS_EQUAL: emit(compiler->function, SRCLANG_BC_LE_I64, expr->line); break;
        default:
          srclang_set_error(compiler->ctx, "[line %d] Unsupported binary operator in native backend.", expr->line);
          return false;
      }
      return true;
    case EXPR_LOGICAL:
      if (!compile_expr(compiler, expr->as.logical.left)) return false;
      if (expr->as.logical.op == TOKEN_AND) {
        int exit_jump = emit(compiler->function, SRCLANG_BC_JUMP_IF_FALSE, expr->line);
        emit(compiler->function, SRCLANG_BC_POP, expr->line);
        if (!compile_expr(compiler, expr->as.logical.right)) return false;
        patch_target(compiler->function, exit_jump, compiler->function->code_count);
        return true;
      }
      srclang_set_error(compiler->ctx, "[line %d] 'or' is not lowered yet in native backend.", expr->line);
      return false;
    case EXPR_CALL: {
      if (is_inline_asm_call(expr)) {
        srclang_set_error(compiler->ctx, "[line %d] asm(...) is a statement and does not produce a value.", expr->line);
        return false;
      }
      if (expr->as.call.callee->kind != EXPR_VARIABLE) {
        srclang_set_error(compiler->ctx, "[line %d] Native backend only supports direct function calls.", expr->line);
        return false;
      }
      for (int i = 0; i < expr->as.call.arg_count; i++) {
        if (!compile_expr(compiler, expr->as.call.args[i])) return false;
      }
      if (expr->as.call.arg_count > 6) {
        srclang_set_error(compiler->ctx, "[line %d] Native backend supports up to 6 register arguments.", expr->line);
        return false;
      }
      int ins = emit(compiler->function, SRCLANG_BC_CALL, expr->line);
      compiler->function->code[ins].operand = expr->as.call.arg_count;
      srclang_native_function_t* callee = program_find_function(compiler->program, expr->as.call.callee->as.variable);
      if (callee != NULL) {
        compiler->function->code[ins].symbol = copy_text(callee->symbol);
        compiler->function->code[ins].operand2 = callee->is_variadic ? 1 : 0;
      } else {
        compiler->function->code[ins].symbol = srclang_native_mangle("srclang_sym_", expr->as.call.callee->as.variable);
      }
      return true;
    }
    case EXPR_GET:
    case EXPR_SET:
    case EXPR_THIS:
    case EXPR_SUPER:
      srclang_set_error(compiler->ctx, "[line %d] Classes/properties are not part of the native assembly backend.", expr->line);
      return false;
  }
  return false;
}

static bool compile_stmt(srclang_native_compiler_t* compiler, const srclang_stmt_t* stmt) {
  switch (stmt->kind) {
    case STMT_EXPR:
      if (is_inline_asm_call(stmt->as.expression)) return emit_inline_asm(compiler, stmt->as.expression);
      if (!compile_expr(compiler, stmt->as.expression)) return false;
      emit(compiler->function, SRCLANG_BC_POP, stmt->line);
      return true;
    case STMT_VAR: {
      int slot = function_add_local(compiler->function, stmt->as.var.name, type_from_annotation(stmt->as.var.type));
      if (stmt->as.var.initializer != NULL) {
        if (!compile_expr(compiler, stmt->as.var.initializer)) return false;
      } else {
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, stmt->line);
        compiler->function->code[ins].imm = 0;
      }
      int store = emit(compiler->function, SRCLANG_BC_STORE_LOCAL, stmt->line);
      compiler->function->code[store].operand = slot;
      emit(compiler->function, SRCLANG_BC_POP, stmt->line);
      return true;
    }
    case STMT_BLOCK:
      for (int i = 0; i < stmt->as.block.count; i++) {
        if (!compile_stmt(compiler, stmt->as.block.declarations[i])) return false;
      }
      return true;
    case STMT_IF: {
      if (!compile_expr(compiler, stmt->as.if_stmt.condition)) return false;
      int then_jump = emit(compiler->function, SRCLANG_BC_JUMP_IF_FALSE, stmt->line);
      if (!compile_stmt(compiler, stmt->as.if_stmt.then_branch)) return false;
      int end_jump = emit(compiler->function, SRCLANG_BC_JUMP, stmt->line);
      patch_target(compiler->function, then_jump, compiler->function->code_count);
      if (stmt->as.if_stmt.else_branch != NULL && !compile_stmt(compiler, stmt->as.if_stmt.else_branch)) return false;
      patch_target(compiler->function, end_jump, compiler->function->code_count);
      return true;
    }
    case STMT_WHILE: {
      int loop_start = compiler->function->code_count;
      if (!compile_expr(compiler, stmt->as.while_stmt.condition)) return false;
      int exit_jump = emit(compiler->function, SRCLANG_BC_JUMP_IF_FALSE, stmt->line);
      if (!compile_stmt(compiler, stmt->as.while_stmt.body)) return false;
      int loop = emit(compiler->function, SRCLANG_BC_JUMP, stmt->line);
      compiler->function->code[loop].operand = loop_start;
      patch_target(compiler->function, exit_jump, compiler->function->code_count);
      return true;
    }
    case STMT_FOR:
      srclang_set_error(compiler->ctx, "[line %d] Native backend does not lower 'for' yet; use while.", stmt->line);
      return false;
    case STMT_RETURN:
      if (stmt->as.return_value != NULL) {
        if (!compile_expr(compiler, stmt->as.return_value)) return false;
      } else {
        int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, stmt->line);
        compiler->function->code[ins].imm = 0;
      }
      emit(compiler->function, SRCLANG_BC_RETURN, stmt->line);
      return true;
    case STMT_FUNCTION:
    case STMT_CLASS:
    case STMT_IMPORT:
      return true;
  }
  return false;
}

static bool compile_function_decl(
    srclang_context_t ctx,
    srclang_native_program_t* program,
    const srclang_function_decl_t* decl) {
  if (decl->is_extern) return true;

  srclang_native_function_t* function = program_find_function(program, decl->name);
  if (function == NULL) function = program_add_function(program, decl->name, false);
  function->arity = decl->param_count;
  function->return_type = type_from_annotation(decl->return_type);
  function->is_variadic = decl->is_variadic;
  for (int i = 0; i < decl->param_count; i++) {
    function_add_local(function, decl->params[i].name, type_from_annotation(decl->params[i].type));
  }

  srclang_native_compiler_t compiler;
  compiler.ctx = ctx;
  compiler.program = program;
  compiler.function = function;
  if (!compile_stmt(&compiler, decl->body)) return false;

  if (function->code_count == 0 || function->code[function->code_count - 1].op != SRCLANG_BC_RETURN) {
    int ins = emit(function, SRCLANG_BC_PUSH_I64, 0);
    function->code[ins].imm = 0;
    emit(function, SRCLANG_BC_RETURN, 0);
  }
  return true;
}

static void declare_function(srclang_native_program_t* program, const srclang_function_decl_t* decl) {
  srclang_native_function_t* function = program_add_function(program, decl->name, decl->is_extern);
  function->arity = decl->param_count;
  function->return_type = type_from_annotation(decl->return_type);
  function->is_variadic = decl->is_variadic;
}

static bool compile_global_initializer(
    srclang_native_compiler_t* compiler,
    const srclang_stmt_t* stmt) {
  if (stmt->as.var.initializer != NULL) {
    if (!compile_expr(compiler, stmt->as.var.initializer)) return false;
  } else {
    int ins = emit(compiler->function, SRCLANG_BC_PUSH_I64, stmt->line);
    compiler->function->code[ins].imm = 0;
  }
  int store = emit(compiler->function, SRCLANG_BC_STORE_GLOBAL, stmt->line);
  compiler->function->code[store].symbol =
      srclang_native_mangle("srclang_global_", stmt->as.var.name);
  emit(compiler->function, SRCLANG_BC_POP, stmt->line);
  return true;
}

bool srclang_native_compile_program_with_entry(
    srclang_context_t ctx,
    const srclang_ast_program_t* ast,
    const char* module_name,
    bool emit_entry,
    srclang_native_program_t* program) {
  srclang_native_program_init(program, module_name);

  for (int i = 0; i < ast->count; i++) {
    const srclang_stmt_t* stmt = ast->declarations[i];
    if (stmt->kind == STMT_VAR) program_add_global(program, stmt->as.var.name, type_from_annotation(stmt->as.var.type));
    if (stmt->kind == STMT_FUNCTION) declare_function(program, &stmt->as.function);
  }

  for (int i = 0; i < ast->count; i++) {
    const srclang_stmt_t* stmt = ast->declarations[i];
    if (stmt->kind == STMT_FUNCTION && !compile_function_decl(ctx, program, &stmt->as.function)) {
      return false;
    }
    if (stmt->kind == STMT_CLASS) {
      srclang_set_error(ctx, "[line %d] Classes are parsed but not lowered by the native assembly backend.", stmt->line);
      return false;
    }
  }

  if (!emit_entry) {
    for (int i = 0; i < ast->count; i++) {
      const srclang_stmt_t* stmt = ast->declarations[i];
      if (stmt->kind == STMT_VAR && !program_set_static_global_initializer(ctx, program, stmt)) return false;
      if (stmt->kind != STMT_VAR && stmt->kind != STMT_FUNCTION && stmt->kind != STMT_IMPORT) {
        srclang_set_error(
            ctx,
            "[line %d] Top-level executable statements require binary or run mode.",
            stmt->line);
        return false;
      }
    }
    return true;
  }

  srclang_native_function_t* main_fn = program_add_function(program, "main", false);
  srclang_native_compiler_t compiler;
  compiler.ctx = ctx;
  compiler.program = program;
  compiler.function = main_fn;

  bool emitted_return = false;
  for (int i = 0; i < ast->count; i++) {
    const srclang_stmt_t* stmt = ast->declarations[i];
    if (stmt->kind == STMT_FUNCTION || stmt->kind == STMT_IMPORT) continue;
    if (stmt->kind == STMT_VAR) {
      if (!compile_global_initializer(&compiler, stmt)) return false;
      continue;
    }
    if (i == ast->count - 1 && stmt->kind == STMT_EXPR && is_inline_asm_call(stmt->as.expression)) {
      if (!compile_stmt(&compiler, stmt)) return false;
      continue;
    }
    if (i == ast->count - 1 && stmt->kind == STMT_EXPR) {
      if (!compile_expr(&compiler, stmt->as.expression)) return false;
      emit(main_fn, SRCLANG_BC_RETURN, stmt->line);
      emitted_return = true;
    } else if (!compile_stmt(&compiler, stmt)) {
      return false;
    }
  }

  if (!emitted_return) {
    int ins = emit(main_fn, SRCLANG_BC_PUSH_I64, 0);
    main_fn->code[ins].imm = 0;
    emit(main_fn, SRCLANG_BC_RETURN, 0);
  }
  return true;
}

bool srclang_native_compile_program(
    srclang_context_t ctx,
    const srclang_ast_program_t* ast,
    const char* module_name,
    srclang_native_program_t* program) {
  return srclang_native_compile_program_with_entry(ctx, ast, module_name, true, program);
}

static const char* opcode_name(srclang_native_opcode_t op) {
  switch (op) {
    case SRCLANG_BC_PUSH_I64: return "push.i64";
    case SRCLANG_BC_PUSH_STR: return "push.str";
    case SRCLANG_BC_LOAD_LOCAL: return "load.local";
    case SRCLANG_BC_STORE_LOCAL: return "store.local";
    case SRCLANG_BC_LOAD_GLOBAL: return "load.global";
    case SRCLANG_BC_STORE_GLOBAL: return "store.global";
    case SRCLANG_BC_POP: return "pop";
    case SRCLANG_BC_NEG_I64: return "neg.i64";
    case SRCLANG_BC_NOT: return "not";
    case SRCLANG_BC_ADD_I64: return "add.i64";
    case SRCLANG_BC_SUB_I64: return "sub.i64";
    case SRCLANG_BC_MUL_I64: return "mul.i64";
    case SRCLANG_BC_DIV_I64: return "div.i64";
    case SRCLANG_BC_MOD_I64: return "mod.i64";
    case SRCLANG_BC_EQ_I64: return "eq.i64";
    case SRCLANG_BC_NE_I64: return "ne.i64";
    case SRCLANG_BC_GT_I64: return "gt.i64";
    case SRCLANG_BC_GE_I64: return "ge.i64";
    case SRCLANG_BC_LT_I64: return "lt.i64";
    case SRCLANG_BC_LE_I64: return "le.i64";
    case SRCLANG_BC_JUMP: return "jump";
    case SRCLANG_BC_JUMP_IF_FALSE: return "jump.false";
    case SRCLANG_BC_CALL: return "call";
    case SRCLANG_BC_INLINE_ASM: return "asm";
    case SRCLANG_BC_RETURN: return "return";
  }
  return "unknown";
}

char* srclang_native_bytecode_dump(srclang_context_t ctx, const srclang_native_program_t* program) {
  (void)ctx;
  srclang_text_writer_t writer;
  writer_init(&writer);
  writer_appendf(&writer, "module %s\n", program->module_name);
  for (int i = 0; i < program->global_count; i++) {
    writer_appendf(&writer, "global %s: %s\n", program->globals[i].name, srclang_native_type_name(program->globals[i].type));
  }
  for (int i = 0; i < program->function_count; i++) {
    const srclang_native_function_t* function = &program->functions[i];
    writer_appendf(&writer, "function %s -> %s\n", function->name, srclang_native_type_name(function->return_type));
    for (int j = 0; j < function->code_count; j++) {
      const srclang_native_instr_t* ins = &function->code[j];
      writer_appendf(&writer, "  %04d %-14s", j, opcode_name(ins->op));
      if (ins->symbol != NULL) writer_appendf(&writer, " %s", ins->symbol);
      else if (ins->op == SRCLANG_BC_PUSH_I64) writer_appendf(&writer, " %lld", ins->imm);
      else if (ins->operand != 0 || ins->op == SRCLANG_BC_LOAD_LOCAL || ins->op == SRCLANG_BC_STORE_LOCAL) {
        writer_appendf(&writer, " %d", ins->operand);
      }
      if (ins->operand2 != 0) writer_appendf(&writer, " %d", ins->operand2);
      writer_append(&writer, "\n");
    }
  }
  return writer.data;
}

#include "ast.h"

#include <stdlib.h>
#include <string.h>

static void* checked_calloc(size_t count, size_t size) {
  void* ptr = calloc(count, size);
  if (ptr == NULL) abort();
  return ptr;
}

char* srclang_ast_copy(const char* chars, int length) {
  char* copy = (char*)checked_calloc((size_t)length + 1, sizeof(char));
  memcpy(copy, chars, (size_t)length);
  copy[length] = '\0';
  return copy;
}

srclang_type_node_t* srclang_type_new(srclang_type_kind_t kind) {
  srclang_type_node_t* type = (srclang_type_node_t*)checked_calloc(1, sizeof(srclang_type_node_t));
  type->kind = kind;
  return type;
}

srclang_expr_t* srclang_expr_new(srclang_expr_kind_t kind, int line) {
  srclang_expr_t* expr = (srclang_expr_t*)checked_calloc(1, sizeof(srclang_expr_t));
  expr->kind = kind;
  expr->line = line;
  return expr;
}

srclang_stmt_t* srclang_stmt_new(srclang_stmt_kind_t kind, int line) {
  srclang_stmt_t* stmt = (srclang_stmt_t*)checked_calloc(1, sizeof(srclang_stmt_t));
  stmt->kind = kind;
  stmt->line = line;
  return stmt;
}

void srclang_ast_program_init(srclang_ast_program_t* program) {
  program->declarations = NULL;
  program->count = 0;
}

void srclang_ast_program_write(srclang_ast_program_t* program, srclang_stmt_t* stmt) {
  srclang_stmt_t** values = (srclang_stmt_t**)realloc(program->declarations, sizeof(srclang_stmt_t*) * (size_t)(program->count + 1));
  if (values == NULL) abort();
  program->declarations = values;
  program->declarations[program->count++] = stmt;
}

static void free_type(srclang_type_node_t* type) {
  if (type == NULL) return;
  switch (type->kind) {
    case TYPE_BUILTIN:
      break;
    case TYPE_NAME:
      free(type->as.name);
      break;
    case TYPE_ARRAY:
    case TYPE_NULLABLE:
    case TYPE_GROUP:
      free_type(type->as.inner);
      break;
    case TYPE_FUNCTION:
      for (int i = 0; i < type->as.function.param_count; i++) {
        free_type(type->as.function.params[i]);
      }
      free(type->as.function.params);
      free_type(type->as.function.result);
      break;
  }
  free(type);
}

static void free_expr(srclang_expr_t* expr) {
  if (expr == NULL) return;
  switch (expr->kind) {
    case EXPR_LITERAL:
      if (expr->as.literal.kind == LITERAL_STRING) free(expr->as.literal.as.string);
      break;
    case EXPR_VARIABLE:
      free(expr->as.variable);
      break;
    case EXPR_ASSIGN:
      free(expr->as.assign.name);
      free_expr(expr->as.assign.value);
      break;
    case EXPR_BINARY:
      free_expr(expr->as.binary.left);
      free_expr(expr->as.binary.right);
      break;
    case EXPR_LOGICAL:
      free_expr(expr->as.logical.left);
      free_expr(expr->as.logical.right);
      break;
    case EXPR_UNARY:
      free_expr(expr->as.unary.right);
      break;
    case EXPR_CALL:
      free_expr(expr->as.call.callee);
      for (int i = 0; i < expr->as.call.arg_count; i++) free_expr(expr->as.call.args[i]);
      free(expr->as.call.args);
      break;
    case EXPR_GET:
      free_expr(expr->as.get.object);
      free(expr->as.get.name);
      break;
    case EXPR_SET:
      free_expr(expr->as.set.object);
      free(expr->as.set.name);
      free_expr(expr->as.set.value);
      break;
    case EXPR_THIS:
      break;
    case EXPR_SUPER:
      free(expr->as.super_method);
      break;
    case EXPR_GROUPING:
      free_expr(expr->as.grouping);
      break;
  }
  free(expr);
}

static void free_function(srclang_function_decl_t* function) {
  free(function->name);
  for (int i = 0; i < function->param_count; i++) {
    free(function->params[i].name);
    free_type(function->params[i].type);
  }
  free(function->params);
  free_type(function->return_type);
}

static void free_stmt(srclang_stmt_t* stmt) {
  if (stmt == NULL) return;
  switch (stmt->kind) {
    case STMT_EXPR:
      free_expr(stmt->as.expression);
      break;
    case STMT_VAR:
      free(stmt->as.var.name);
      free_type(stmt->as.var.type);
      free_expr(stmt->as.var.initializer);
      break;
    case STMT_BLOCK:
      for (int i = 0; i < stmt->as.block.count; i++) free_stmt(stmt->as.block.declarations[i]);
      free(stmt->as.block.declarations);
      break;
    case STMT_IF:
      free_expr(stmt->as.if_stmt.condition);
      free_stmt(stmt->as.if_stmt.then_branch);
      free_stmt(stmt->as.if_stmt.else_branch);
      break;
    case STMT_WHILE:
      free_expr(stmt->as.while_stmt.condition);
      free_stmt(stmt->as.while_stmt.body);
      break;
    case STMT_FOR:
      free_stmt(stmt->as.for_stmt.initializer);
      free_expr(stmt->as.for_stmt.condition);
      free_expr(stmt->as.for_stmt.increment);
      free_stmt(stmt->as.for_stmt.body);
      break;
    case STMT_RETURN:
      free_expr(stmt->as.return_value);
      break;
    case STMT_FUNCTION:
      free_function(&stmt->as.function);
      free_stmt(stmt->as.function.body);
      break;
    case STMT_CLASS:
      free(stmt->as.class_decl.name);
      free(stmt->as.class_decl.superclass);
      for (int i = 0; i < stmt->as.class_decl.member_count; i++) {
        srclang_class_member_t* member = &stmt->as.class_decl.members[i];
        if (member->kind == CLASS_MEMBER_FIELD) {
          free(member->as.field.name);
          free_type(member->as.field.type);
          free_expr(member->as.field.initializer);
        } else {
          free_function(&member->as.method.function);
          free_stmt(member->as.method.function.body);
        }
      }
      free(stmt->as.class_decl.members);
      break;
    case STMT_IMPORT:
      free(stmt->as.import_path);
      break;
  }
  free(stmt);
}

void srclang_ast_program_free(srclang_ast_program_t* program) {
  for (int i = 0; i < program->count; i++) free_stmt(program->declarations[i]);
  free(program->declarations);
  srclang_ast_program_init(program);
}

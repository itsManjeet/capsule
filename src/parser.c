#include "parser.h"

#include "scanner.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  srclang_scanner_t scanner;
  srclang_token_t current;
  srclang_token_t previous;
  bool had_error;
  bool panic_mode;
  srclang_context_t ctx;
} srclang_parser_t;

static srclang_stmt_t* declaration(srclang_parser_t* parser);
static srclang_stmt_t* top_level_declaration(srclang_parser_t* parser);
static srclang_stmt_t* statement(srclang_parser_t* parser);
static srclang_expr_t* expression(srclang_parser_t* parser);
static srclang_type_node_t* parse_type(srclang_parser_t* parser);

static bool check(srclang_parser_t* parser, srclang_token_type_t type) {
  return parser->current.type == type;
}

static bool is_at_end(srclang_parser_t* parser) {
  return parser->current.type == TOKEN_EOF;
}

static void error_at(srclang_parser_t* parser, srclang_token_t* token, const char* format, ...) {
  if (parser->panic_mode) return;
  parser->panic_mode = true;
  parser->had_error = true;

  char detail[512];
  va_list args;
  va_start(args, format);
  vsnprintf(detail, sizeof(detail), format, args);
  va_end(args);

  if (token->type == TOKEN_EOF) {
    srclang_set_error(parser->ctx, "[line %d:%d] Error at end: %s", token->line, token->column, detail);
  } else if (token->type == TOKEN_ERROR) {
    srclang_set_error(parser->ctx, "[line %d:%d] Error: %s", token->line, token->column, detail);
  } else {
    srclang_set_error(
        parser->ctx,
        "[line %d:%d] Error at '%.*s': %s",
        token->line,
        token->column,
        token->length,
        token->start,
        detail);
  }
}

static void error(srclang_parser_t* parser, const char* message) {
  error_at(parser, &parser->previous, "%s", message);
}

static void error_at_current(srclang_parser_t* parser, const char* message) {
  error_at(parser, &parser->current, "%s", message);
}

static void advance(srclang_parser_t* parser) {
  parser->previous = parser->current;
  for (;;) {
    parser->current = srclang_scan_token(&parser->scanner);
    if (parser->current.type != TOKEN_ERROR) break;
    error_at_current(parser, parser->current.start);
  }
}

static bool match(srclang_parser_t* parser, srclang_token_type_t type) {
  if (!check(parser, type)) return false;
  advance(parser);
  return true;
}

static srclang_token_t consume(srclang_parser_t* parser, srclang_token_type_t type, const char* message) {
  if (check(parser, type)) {
    advance(parser);
    return parser->previous;
  }
  error_at_current(parser, message);
  return parser->current;
}

static char* copy_token(srclang_token_t token) {
  return srclang_ast_copy(token.start, token.length);
}

static void append_stmt(srclang_stmt_t*** values, int* count, srclang_stmt_t* stmt) {
  srclang_stmt_t** grown = (srclang_stmt_t**)realloc(*values, sizeof(srclang_stmt_t*) * (size_t)(*count + 1));
  if (grown == NULL) abort();
  *values = grown;
  (*values)[(*count)++] = stmt;
}

static void append_expr(srclang_expr_t*** values, int* count, srclang_expr_t* expr) {
  srclang_expr_t** grown = (srclang_expr_t**)realloc(*values, sizeof(srclang_expr_t*) * (size_t)(*count + 1));
  if (grown == NULL) abort();
  *values = grown;
  (*values)[(*count)++] = expr;
}

static void append_param(srclang_param_t** values, int* count, srclang_param_t param) {
  srclang_param_t* grown = (srclang_param_t*)realloc(*values, sizeof(srclang_param_t) * (size_t)(*count + 1));
  if (grown == NULL) abort();
  *values = grown;
  (*values)[(*count)++] = param;
}

static void append_member(srclang_class_member_t** values, int* count, srclang_class_member_t member) {
  srclang_class_member_t* grown = (srclang_class_member_t*)realloc(*values, sizeof(srclang_class_member_t) * (size_t)(*count + 1));
  if (grown == NULL) abort();
  *values = grown;
  (*values)[(*count)++] = member;
}

static void append_type(srclang_type_node_t*** values, int* count, srclang_type_node_t* type) {
  srclang_type_node_t** grown = (srclang_type_node_t**)realloc(*values, sizeof(srclang_type_node_t*) * (size_t)(*count + 1));
  if (grown == NULL) abort();
  *values = grown;
  (*values)[(*count)++] = type;
}

static bool is_legacy_builtin_type_name(srclang_token_t token) {
  return (token.length == 3 && memcmp(token.start, "Any", 3) == 0) ||
         (token.length == 3 && memcmp(token.start, "Nil", 3) == 0) ||
         (token.length == 4 && memcmp(token.start, "Bool", 4) == 0) ||
         (token.length == 6 && memcmp(token.start, "Number", 6) == 0) ||
         (token.length == 6 && memcmp(token.start, "String", 6) == 0) ||
         (token.length == 4 && memcmp(token.start, "Void", 4) == 0);
}

static bool builtin_from_name(srclang_token_t token, srclang_builtin_type_t* builtin) {
  if (token.type == TOKEN_NIL) {
    *builtin = BUILTIN_NIL;
    return true;
  }
  if (token.type != TOKEN_IDENTIFIER) return false;

  if (token.length == 3 && memcmp(token.start, "any", 3) == 0) *builtin = BUILTIN_ANY;
  else if (token.length == 4 && memcmp(token.start, "bool", 4) == 0) *builtin = BUILTIN_BOOL;
  else if (token.length == 3 && memcmp(token.start, "num", 3) == 0) *builtin = BUILTIN_NUM;
  else if (token.length == 2 && memcmp(token.start, "u8", 2) == 0) *builtin = BUILTIN_U8;
  else if (token.length == 3 && memcmp(token.start, "u16", 3) == 0) *builtin = BUILTIN_U16;
  else if (token.length == 3 && memcmp(token.start, "u32", 3) == 0) *builtin = BUILTIN_U32;
  else if (token.length == 3 && memcmp(token.start, "u64", 3) == 0) *builtin = BUILTIN_U64;
  else if (token.length == 2 && memcmp(token.start, "i8", 2) == 0) *builtin = BUILTIN_I8;
  else if (token.length == 3 && memcmp(token.start, "i16", 3) == 0) *builtin = BUILTIN_I16;
  else if (token.length == 3 && memcmp(token.start, "i32", 3) == 0) *builtin = BUILTIN_I32;
  else if (token.length == 3 && memcmp(token.start, "i64", 3) == 0) *builtin = BUILTIN_I64;
  else if (token.length == 3 && memcmp(token.start, "f32", 3) == 0) *builtin = BUILTIN_F32;
  else if (token.length == 3 && memcmp(token.start, "f64", 3) == 0) *builtin = BUILTIN_F64;
  else if (token.length == 3 && memcmp(token.start, "str", 3) == 0) *builtin = BUILTIN_STR;
  else if (token.length == 6 && memcmp(token.start, "string", 6) == 0) *builtin = BUILTIN_STRING;
  else if (token.length == 3 && memcmp(token.start, "ptr", 3) == 0) *builtin = BUILTIN_PTR;
  else if (token.length == 4 && memcmp(token.start, "none", 4) == 0) *builtin = BUILTIN_NONE;
  else return false;

  return true;
}

static srclang_type_node_t* primary_type(srclang_parser_t* parser) {
  srclang_builtin_type_t builtin;
  if (builtin_from_name(parser->current, &builtin)) {
    advance(parser);
    srclang_type_node_t* type = srclang_type_new(TYPE_BUILTIN);
    type->as.builtin = builtin;
    return type;
  }

  if (match(parser, TOKEN_IDENTIFIER)) {
    if (is_legacy_builtin_type_name(parser->previous)) {
      error(parser, "Builtin type names are lowercase: any, nil, bool, num, string, none.");
    }
    srclang_type_node_t* type = srclang_type_new(TYPE_NAME);
    type->as.name = copy_token(parser->previous);
    return type;
  }

  if (match(parser, TOKEN_LEFT_BRACKET)) {
    srclang_type_node_t* item = parse_type(parser);
    consume(parser, TOKEN_RIGHT_BRACKET, "Expected ']' after array type.");
    srclang_type_node_t* type = srclang_type_new(TYPE_ARRAY);
    type->as.inner = item;
    return type;
  }

  if (match(parser, TOKEN_LEFT_PAREN)) {
    srclang_type_node_t* inner = parse_type(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after grouped type.");
    srclang_type_node_t* type = srclang_type_new(TYPE_GROUP);
    type->as.inner = inner;
    return type;
  }

  error_at_current(parser, "Expected type.");
  srclang_type_node_t* fallback = srclang_type_new(TYPE_BUILTIN);
  fallback->as.builtin = BUILTIN_ANY;
  return fallback;
}

static srclang_type_node_t* nullable_type(srclang_parser_t* parser) {
  srclang_type_node_t* inner = primary_type(parser);
  if (match(parser, TOKEN_QUESTION)) {
    srclang_type_node_t* type = srclang_type_new(TYPE_NULLABLE);
    type->as.inner = inner;
    return type;
  }
  return inner;
}

static srclang_type_node_t* function_type(srclang_parser_t* parser) {
  srclang_token_t fun = consume(parser, TOKEN_FUN, "Expected 'fun' in function type.");
  SRCLANG_UNUSED(fun);
  consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'fun' in function type.");

  srclang_type_node_t** params = NULL;
  int param_count = 0;
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      append_type(&params, &param_count, parse_type(parser));
    } while (match(parser, TOKEN_COMMA));
  }

  consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after function type parameters.");
  consume(parser, TOKEN_COLON, "Expected ':' before function type result.");
  srclang_type_node_t* result = parse_type(parser);

  srclang_type_node_t* type = srclang_type_new(TYPE_FUNCTION);
  type->as.function.params = params;
  type->as.function.param_count = param_count;
  type->as.function.result = result;
  return type;
}

static srclang_type_node_t* parse_type(srclang_parser_t* parser) {
  if (check(parser, TOKEN_FUN)) return function_type(parser);
  return nullable_type(parser);
}

static srclang_type_node_t* type_annotation(srclang_parser_t* parser) {
  if (!match(parser, TOKEN_COLON)) return NULL;
  return parse_type(parser);
}

static char* string_literal(srclang_parser_t* parser, srclang_token_t token) {
  SRCLANG_UNUSED(parser);
  int raw_length = token.length - 2;
  const char* raw = token.start + 1;
  char* value = (char*)calloc((size_t)raw_length + 1, sizeof(char));
  if (value == NULL) abort();

  int out = 0;
  for (int i = 0; i < raw_length; i++) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw_length) {
      char escaped = raw[++i];
      switch (escaped) {
        case 'n': value[out++] = '\n'; break;
        case 'r': value[out++] = '\r'; break;
        case 't': value[out++] = '\t'; break;
        case '"': value[out++] = '"'; break;
        case '\\': value[out++] = '\\'; break;
        default: value[out++] = escaped; break;
      }
    } else {
      value[out++] = c;
    }
  }
  value[out] = '\0';
  return value;
}

static srclang_expr_t* primary(srclang_parser_t* parser) {
  if (match(parser, TOKEN_FALSE)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->previous.line);
    expr->as.literal.kind = LITERAL_BOOL;
    expr->as.literal.as.boolean = false;
    return expr;
  }
  if (match(parser, TOKEN_TRUE)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->previous.line);
    expr->as.literal.kind = LITERAL_BOOL;
    expr->as.literal.as.boolean = true;
    return expr;
  }
  if (match(parser, TOKEN_NIL)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->previous.line);
    expr->as.literal.kind = LITERAL_NIL;
    return expr;
  }
  if (match(parser, TOKEN_THIS)) {
    return srclang_expr_new(EXPR_THIS, parser->previous.line);
  }
  if (match(parser, TOKEN_SUPER)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_SUPER, parser->previous.line);
    consume(parser, TOKEN_DOT, "Expected '.' after 'super'.");
    srclang_token_t method = consume(parser, TOKEN_IDENTIFIER, "Expected superclass method name.");
    expr->as.super_method = copy_token(method);
    return expr;
  }
  if (match(parser, TOKEN_NUMBER)) {
    char* number = copy_token(parser->previous);
    srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->previous.line);
    expr->as.literal.kind = LITERAL_NUMBER;
    expr->as.literal.as.number = strtod(number, NULL);
    free(number);
    return expr;
  }
  if (match(parser, TOKEN_STRING)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->previous.line);
    expr->as.literal.kind = LITERAL_STRING;
    expr->as.literal.as.string = string_literal(parser, parser->previous);
    return expr;
  }
  if (match(parser, TOKEN_IDENTIFIER)) {
    srclang_expr_t* expr = srclang_expr_new(EXPR_VARIABLE, parser->previous.line);
    expr->as.variable = copy_token(parser->previous);
    return expr;
  }
  if (match(parser, TOKEN_LEFT_PAREN)) {
    srclang_expr_t* inner = expression(parser);
    consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
    srclang_expr_t* expr = srclang_expr_new(EXPR_GROUPING, parser->previous.line);
    expr->as.grouping = inner;
    return expr;
  }

  error_at_current(parser, "Expected expression.");
  srclang_expr_t* expr = srclang_expr_new(EXPR_LITERAL, parser->current.line);
  expr->as.literal.kind = LITERAL_NIL;
  return expr;
}

static srclang_expr_t* finish_call(srclang_parser_t* parser, srclang_expr_t* callee) {
  srclang_expr_t** args = NULL;
  int arg_count = 0;
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      append_expr(&args, &arg_count, expression(parser));
    } while (match(parser, TOKEN_COMMA));
  }
  srclang_token_t paren = consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
  srclang_expr_t* expr = srclang_expr_new(EXPR_CALL, paren.line);
  expr->as.call.callee = callee;
  expr->as.call.args = args;
  expr->as.call.arg_count = arg_count;
  return expr;
}

static srclang_expr_t* call(srclang_parser_t* parser) {
  srclang_expr_t* expr = primary(parser);
  for (;;) {
    if (match(parser, TOKEN_LEFT_PAREN)) {
      expr = finish_call(parser, expr);
    } else if (match(parser, TOKEN_DOT)) {
      srclang_token_t name = consume(parser, TOKEN_IDENTIFIER, "Expected property name after '.'.");
      srclang_expr_t* get = srclang_expr_new(EXPR_GET, name.line);
      get->as.get.object = expr;
      get->as.get.name = copy_token(name);
      expr = get;
    } else {
      break;
    }
  }
  return expr;
}

static srclang_expr_t* unary(srclang_parser_t* parser) {
  if (match(parser, TOKEN_BANG) || match(parser, TOKEN_MINUS)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = unary(parser);
    srclang_expr_t* expr = srclang_expr_new(EXPR_UNARY, op.line);
    expr->as.unary.op = op.type;
    expr->as.unary.right = right;
    return expr;
  }
  return call(parser);
}

static srclang_expr_t* factor(srclang_parser_t* parser) {
  srclang_expr_t* expr = unary(parser);
  while (match(parser, TOKEN_SLASH) || match(parser, TOKEN_STAR) || match(parser, TOKEN_PERCENT)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = unary(parser);
    srclang_expr_t* binary = srclang_expr_new(EXPR_BINARY, op.line);
    binary->as.binary.left = expr;
    binary->as.binary.op = op.type;
    binary->as.binary.right = right;
    expr = binary;
  }
  return expr;
}

static srclang_expr_t* term(srclang_parser_t* parser) {
  srclang_expr_t* expr = factor(parser);
  while (match(parser, TOKEN_MINUS) || match(parser, TOKEN_PLUS)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = factor(parser);
    srclang_expr_t* binary = srclang_expr_new(EXPR_BINARY, op.line);
    binary->as.binary.left = expr;
    binary->as.binary.op = op.type;
    binary->as.binary.right = right;
    expr = binary;
  }
  return expr;
}

static srclang_expr_t* comparison(srclang_parser_t* parser) {
  srclang_expr_t* expr = term(parser);
  while (match(parser, TOKEN_GREATER) || match(parser, TOKEN_GREATER_EQUAL) ||
         match(parser, TOKEN_LESS) || match(parser, TOKEN_LESS_EQUAL)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = term(parser);
    srclang_expr_t* binary = srclang_expr_new(EXPR_BINARY, op.line);
    binary->as.binary.left = expr;
    binary->as.binary.op = op.type;
    binary->as.binary.right = right;
    expr = binary;
  }
  return expr;
}

static srclang_expr_t* equality(srclang_parser_t* parser) {
  srclang_expr_t* expr = comparison(parser);
  while (match(parser, TOKEN_BANG_EQUAL) || match(parser, TOKEN_EQUAL_EQUAL)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = comparison(parser);
    srclang_expr_t* binary = srclang_expr_new(EXPR_BINARY, op.line);
    binary->as.binary.left = expr;
    binary->as.binary.op = op.type;
    binary->as.binary.right = right;
    expr = binary;
  }
  return expr;
}

static srclang_expr_t* logic_and(srclang_parser_t* parser) {
  srclang_expr_t* expr = equality(parser);
  while (match(parser, TOKEN_AND)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = equality(parser);
    srclang_expr_t* logical = srclang_expr_new(EXPR_LOGICAL, op.line);
    logical->as.logical.left = expr;
    logical->as.logical.op = op.type;
    logical->as.logical.right = right;
    expr = logical;
  }
  return expr;
}

static srclang_expr_t* logic_or(srclang_parser_t* parser) {
  srclang_expr_t* expr = logic_and(parser);
  while (match(parser, TOKEN_OR)) {
    srclang_token_t op = parser->previous;
    srclang_expr_t* right = logic_and(parser);
    srclang_expr_t* logical = srclang_expr_new(EXPR_LOGICAL, op.line);
    logical->as.logical.left = expr;
    logical->as.logical.op = op.type;
    logical->as.logical.right = right;
    expr = logical;
  }
  return expr;
}

static srclang_expr_t* assignment(srclang_parser_t* parser) {
  srclang_expr_t* expr = logic_or(parser);
  if (match(parser, TOKEN_EQUAL)) {
    srclang_token_t equals = parser->previous;
    srclang_expr_t* value = assignment(parser);
    if (expr->kind == EXPR_VARIABLE) {
      char* name = expr->as.variable;
      free(expr);
      srclang_expr_t* assign = srclang_expr_new(EXPR_ASSIGN, equals.line);
      assign->as.assign.name = name;
      assign->as.assign.value = value;
      return assign;
    }
    if (expr->kind == EXPR_GET) {
      srclang_expr_t* object = expr->as.get.object;
      char* name = expr->as.get.name;
      free(expr);
      srclang_expr_t* set = srclang_expr_new(EXPR_SET, equals.line);
      set->as.set.object = object;
      set->as.set.name = name;
      set->as.set.value = value;
      return set;
    }
    error(parser, "Invalid assignment target.");
  }
  return expr;
}

static srclang_expr_t* expression(srclang_parser_t* parser) {
  return assignment(parser);
}

static srclang_stmt_t* block_after_left_brace(srclang_parser_t* parser, int line) {
  srclang_stmt_t** declarations = NULL;
  int count = 0;
  while (!check(parser, TOKEN_RIGHT_BRACE) && !is_at_end(parser)) {
    append_stmt(&declarations, &count, declaration(parser));
  }
  consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after block.");
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_BLOCK, line);
  stmt->as.block.declarations = declarations;
  stmt->as.block.count = count;
  return stmt;
}

static void parse_parameters(
    srclang_parser_t* parser,
    bool allow_variadic,
    srclang_param_t** params,
    int* param_count,
    bool* is_variadic) {
  if (check(parser, TOKEN_RIGHT_PAREN)) return;

  do {
    if (match(parser, TOKEN_ELLIPSIS)) {
      if (!allow_variadic) {
        error(parser, "Variadic parameters are only allowed in extern function declarations.");
      }
      *is_variadic = true;
      if (!check(parser, TOKEN_RIGHT_PAREN)) {
        error_at_current(parser, "'...' must be the last extern parameter.");
      }
      break;
    }

    srclang_token_t param_name = consume(parser, TOKEN_IDENTIFIER, "Expected parameter name.");
    srclang_param_t param;
    param.name = copy_token(param_name);
    param.type = type_annotation(parser);
    append_param(params, param_count, param);
  } while (match(parser, TOKEN_COMMA));
}

static srclang_function_decl_t function_signature(srclang_parser_t* parser, bool allow_variadic) {
  srclang_token_t name = consume(parser, TOKEN_IDENTIFIER, "Expected function name.");
  consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after function name.");

  srclang_param_t* params = NULL;
  int param_count = 0;
  bool is_variadic = false;
  parse_parameters(parser, allow_variadic, &params, &param_count, &is_variadic);

  consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

  srclang_function_decl_t function;
  memset(&function, 0, sizeof(function));
  function.name = copy_token(name);
  function.params = params;
  function.param_count = param_count;
  function.is_variadic = is_variadic;
  return function;
}

static srclang_function_decl_t function_decl(srclang_parser_t* parser, const char* kind) {
  srclang_function_decl_t function = function_signature(parser, false);
  function.return_type = type_annotation(parser);
  consume(parser, TOKEN_LEFT_BRACE, kind);
  function.body = block_after_left_brace(parser, parser->previous.line);
  return function;
}

static srclang_stmt_t* function_declaration(srclang_parser_t* parser) {
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_FUNCTION, parser->previous.line);
  stmt->as.function = function_decl(parser, "Expected '{' before function body.");
  return stmt;
}

static srclang_type_node_t* extern_return_type(srclang_parser_t* parser) {
  if (match(parser, TOKEN_COLON)) return parse_type(parser);
  if (!check(parser, TOKEN_SEMICOLON)) return parse_type(parser);
  return NULL;
}

static srclang_stmt_t* extern_function_declaration(srclang_parser_t* parser) {
  int line = parser->previous.line;
  consume(parser, TOKEN_FUN, "Expected 'fun' after 'extern'.");

  srclang_stmt_t* stmt = srclang_stmt_new(STMT_FUNCTION, line);
  stmt->as.function = function_signature(parser, true);
  stmt->as.function.is_extern = true;
  stmt->as.function.return_type = extern_return_type(parser);
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after extern function declaration.");
  return stmt;
}

static srclang_stmt_t* var_declaration(srclang_parser_t* parser) {
  srclang_token_t name = consume(parser, TOKEN_IDENTIFIER, "Expected variable name.");
  srclang_type_node_t* type = type_annotation(parser);
  srclang_expr_t* initializer = NULL;
  if (match(parser, TOKEN_EQUAL)) initializer = expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

  srclang_stmt_t* stmt = srclang_stmt_new(STMT_VAR, name.line);
  stmt->as.var.name = copy_token(name);
  stmt->as.var.type = type;
  stmt->as.var.initializer = initializer;
  return stmt;
}

static srclang_stmt_t* import_declaration(srclang_parser_t* parser) {
  srclang_token_t first = consume(parser, TOKEN_IDENTIFIER, "Expected module name after 'import'.");
  size_t capacity = (size_t)first.length + 1;
  size_t length = 0;
  char* path = (char*)malloc(capacity);
  if (path == NULL) abort();

  memcpy(path, first.start, (size_t)first.length);
  length = (size_t)first.length;
  path[length] = '\0';

  while (match(parser, TOKEN_DOT)) {
    srclang_token_t segment = consume(parser, TOKEN_IDENTIFIER, "Expected module path segment after '.'.");
    size_t needed = length + 1 + (size_t)segment.length + 1;
    if (capacity < needed) {
      while (capacity < needed) capacity *= 2;
      char* grown = (char*)realloc(path, capacity);
      if (grown == NULL) abort();
      path = grown;
    }
    path[length++] = '.';
    memcpy(path + length, segment.start, (size_t)segment.length);
    length += (size_t)segment.length;
    path[length] = '\0';
  }

  consume(parser, TOKEN_SEMICOLON, "Expected ';' after import declaration.");
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_IMPORT, first.line);
  stmt->as.import_path = path;
  return stmt;
}

static srclang_class_member_t class_member(srclang_parser_t* parser) {
  srclang_class_member_t member;
  memset(&member, 0, sizeof(member));
  member.line = parser->current.line;

  if (match(parser, TOKEN_LET)) {
    srclang_token_t name = consume(parser, TOKEN_IDENTIFIER, "Expected field name.");
    member.kind = CLASS_MEMBER_FIELD;
    member.as.field.name = copy_token(name);
    member.as.field.type = type_annotation(parser);
    if (match(parser, TOKEN_EQUAL)) member.as.field.initializer = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expected ';' after field declaration.");
    return member;
  }

  bool is_static = match(parser, TOKEN_CLASS);
  member.kind = CLASS_MEMBER_METHOD;
  member.as.method.is_static = is_static;
  member.as.method.function = function_decl(parser, "Expected '{' before method body.");
  return member;
}

static srclang_stmt_t* class_declaration(srclang_parser_t* parser) {
  srclang_token_t name = consume(parser, TOKEN_IDENTIFIER, "Expected class name.");
  char* superclass = NULL;
  if (match(parser, TOKEN_LESS)) {
    srclang_token_t super_name = consume(parser, TOKEN_IDENTIFIER, "Expected superclass name.");
    superclass = copy_token(super_name);
  }

  consume(parser, TOKEN_LEFT_BRACE, "Expected '{' before class body.");
  srclang_class_member_t* members = NULL;
  int member_count = 0;
  while (!check(parser, TOKEN_RIGHT_BRACE) && !is_at_end(parser)) {
    append_member(&members, &member_count, class_member(parser));
  }
  consume(parser, TOKEN_RIGHT_BRACE, "Expected '}' after class body.");

  srclang_stmt_t* stmt = srclang_stmt_new(STMT_CLASS, name.line);
  stmt->as.class_decl.name = copy_token(name);
  stmt->as.class_decl.superclass = superclass;
  stmt->as.class_decl.members = members;
  stmt->as.class_decl.member_count = member_count;
  return stmt;
}

static srclang_stmt_t* expression_statement(srclang_parser_t* parser) {
  srclang_expr_t* expr = expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after expression.");
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_EXPR, expr->line);
  stmt->as.expression = expr;
  return stmt;
}

static srclang_stmt_t* if_statement(srclang_parser_t* parser) {
  int line = parser->previous.line;
  consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
  srclang_expr_t* condition = expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after if condition.");
  srclang_stmt_t* then_branch = statement(parser);
  srclang_stmt_t* else_branch = NULL;
  if (match(parser, TOKEN_ELSE)) else_branch = statement(parser);

  srclang_stmt_t* stmt = srclang_stmt_new(STMT_IF, line);
  stmt->as.if_stmt.condition = condition;
  stmt->as.if_stmt.then_branch = then_branch;
  stmt->as.if_stmt.else_branch = else_branch;
  return stmt;
}

static srclang_stmt_t* while_statement(srclang_parser_t* parser) {
  int line = parser->previous.line;
  consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
  srclang_expr_t* condition = expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after while condition.");
  srclang_stmt_t* body = statement(parser);

  srclang_stmt_t* stmt = srclang_stmt_new(STMT_WHILE, line);
  stmt->as.while_stmt.condition = condition;
  stmt->as.while_stmt.body = body;
  return stmt;
}

static srclang_stmt_t* return_statement(srclang_parser_t* parser) {
  int line = parser->previous.line;
  srclang_expr_t* value = NULL;
  if (!check(parser, TOKEN_SEMICOLON)) value = expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after return value.");
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_RETURN, line);
  stmt->as.return_value = value;
  return stmt;
}

static srclang_stmt_t* for_statement(srclang_parser_t* parser) {
  int line = parser->previous.line;
  consume(parser, TOKEN_LEFT_PAREN, "Expected '(' after 'for'.");

  srclang_stmt_t* initializer = NULL;
  if (match(parser, TOKEN_SEMICOLON)) {
    initializer = NULL;
  } else if (match(parser, TOKEN_LET)) {
    initializer = var_declaration(parser);
  } else {
    initializer = expression_statement(parser);
  }

  srclang_expr_t* condition = NULL;
  if (!check(parser, TOKEN_SEMICOLON)) condition = expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expected ';' after loop condition.");

  srclang_expr_t* increment = NULL;
  if (!check(parser, TOKEN_RIGHT_PAREN)) increment = expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expected ')' after for clauses.");

  srclang_stmt_t* body = statement(parser);
  srclang_stmt_t* stmt = srclang_stmt_new(STMT_FOR, line);
  stmt->as.for_stmt.initializer = initializer;
  stmt->as.for_stmt.condition = condition;
  stmt->as.for_stmt.increment = increment;
  stmt->as.for_stmt.body = body;
  return stmt;
}

static srclang_stmt_t* statement(srclang_parser_t* parser) {
  if (match(parser, TOKEN_FOR)) return for_statement(parser);
  if (match(parser, TOKEN_IF)) return if_statement(parser);
  if (match(parser, TOKEN_RETURN)) return return_statement(parser);
  if (match(parser, TOKEN_WHILE)) return while_statement(parser);
  if (match(parser, TOKEN_LEFT_BRACE)) return block_after_left_brace(parser, parser->previous.line);
  return expression_statement(parser);
}

static srclang_stmt_t* declaration(srclang_parser_t* parser) {
  parser->panic_mode = false;
  if (match(parser, TOKEN_CLASS)) return class_declaration(parser);
  if (match(parser, TOKEN_EXTERN)) return extern_function_declaration(parser);
  if (match(parser, TOKEN_FUN)) return function_declaration(parser);
  if (match(parser, TOKEN_LET)) return var_declaration(parser);
  return statement(parser);
}

static srclang_stmt_t* top_level_declaration(srclang_parser_t* parser) {
  parser->panic_mode = false;
  if (match(parser, TOKEN_IMPORT)) return import_declaration(parser);
  return declaration(parser);
}

bool srclang_parse_source(srclang_context_t ctx, const char* source, srclang_ast_program_t* program) {
  srclang_parser_t parser;
  memset(&parser, 0, sizeof(parser));
  parser.ctx = ctx;
  srclang_scanner_init(&parser.scanner, source);
  srclang_ast_program_init(program);

  advance(&parser);
  while (!match(&parser, TOKEN_EOF)) {
    srclang_ast_program_write(program, top_level_declaration(&parser));
    if (parser.had_error) break;
  }

  if (parser.had_error) {
    srclang_ast_program_free(program);
    return false;
  }
  return true;
}

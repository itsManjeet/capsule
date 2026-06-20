#ifndef SRCLANG_SCANNER_H
#define SRCLANG_SCANNER_H

#include "common.h"

typedef enum {
  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_LEFT_BRACKET,
  TOKEN_RIGHT_BRACKET,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_ELLIPSIS,
  TOKEN_MINUS,
  TOKEN_PLUS,
  TOKEN_SEMICOLON,
  TOKEN_SLASH,
  TOKEN_STAR,
  TOKEN_PERCENT,
  TOKEN_COLON,
  TOKEN_QUESTION,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,
  TOKEN_IDENTIFIER,
  TOKEN_STRING,
  TOKEN_NUMBER,
  TOKEN_AND,
  TOKEN_CLASS,
  TOKEN_ELSE,
  TOKEN_EXTERN,
  TOKEN_FALSE,
  TOKEN_FOR,
  TOKEN_FUN,
  TOKEN_IF,
  TOKEN_IMPORT,
  TOKEN_LET,
  TOKEN_NIL,
  TOKEN_OR,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_TRUE,
  TOKEN_WHILE,
  TOKEN_TYPE_ANY,
  TOKEN_TYPE_BOOL,
  TOKEN_TYPE_NUM,
  TOKEN_TYPE_STRING,
  TOKEN_TYPE_NONE,
  TOKEN_ERROR,
  TOKEN_EOF
} srclang_token_type_t;

typedef struct {
  srclang_token_type_t type;
  const char* start;
  int length;
  int line;
  int column;
} srclang_token_t;

typedef struct {
  const char* start;
  const char* current;
  int line;
  int column;
  int token_column;
} srclang_scanner_t;

void srclang_scanner_init(srclang_scanner_t* scanner, const char* source);
srclang_token_t srclang_scan_token(srclang_scanner_t* scanner);

#endif

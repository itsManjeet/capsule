#include "scanner.h"

#include <string.h>

void srclang_scanner_init(srclang_scanner_t* scanner, const char* source) {
  scanner->start = source;
  scanner->current = source;
  scanner->line = 1;
  scanner->column = 1;
  scanner->token_column = 1;
}

static bool is_at_end(srclang_scanner_t* scanner) {
  return *scanner->current == '\0';
}

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         c == '_';
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static char advance(srclang_scanner_t* scanner) {
  char c = *scanner->current++;
  if (c == '\n') {
    scanner->line++;
    scanner->column = 1;
  } else {
    scanner->column++;
  }
  return c;
}

static char peek(srclang_scanner_t* scanner) {
  return *scanner->current;
}

static char peek_next(srclang_scanner_t* scanner) {
  if (is_at_end(scanner)) return '\0';
  return scanner->current[1];
}

static bool match(srclang_scanner_t* scanner, char expected) {
  if (is_at_end(scanner)) return false;
  if (*scanner->current != expected) return false;
  scanner->current++;
  scanner->column++;
  return true;
}

static srclang_token_t make_token(srclang_scanner_t* scanner, srclang_token_type_t type) {
  srclang_token_t token;
  token.type = type;
  token.start = scanner->start;
  token.length = (int)(scanner->current - scanner->start);
  token.line = scanner->line;
  token.column = scanner->token_column;
  return token;
}

static srclang_token_t error_token(srclang_scanner_t* scanner, const char* message) {
  srclang_token_t token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner->line;
  token.column = scanner->column;
  return token;
}

static void skip_whitespace(srclang_scanner_t* scanner) {
  for (;;) {
    char c = peek(scanner);
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance(scanner);
        break;
      case '\n':
        advance(scanner);
        break;
      case '/':
        if (peek_next(scanner) == '/') {
          while (peek(scanner) != '\n' && !is_at_end(scanner)) advance(scanner);
        } else if (peek_next(scanner) == '*') {
          advance(scanner);
          advance(scanner);
          while (!is_at_end(scanner)) {
            if (peek(scanner) == '*' && peek_next(scanner) == '/') {
              advance(scanner);
              advance(scanner);
              break;
            }
            advance(scanner);
          }
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

static srclang_token_type_t check_keyword(
    srclang_scanner_t* scanner,
    int start,
    int length,
    const char* rest,
    srclang_token_type_t type) {
  if (scanner->current - scanner->start == start + length &&
      memcmp(scanner->start + start, rest, (size_t)length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static srclang_token_type_t identifier_type(srclang_scanner_t* scanner) {
  switch (scanner->start[0]) {
    case 'a':
      if (scanner->current - scanner->start > 1) {
        if (scanner->start[1] == 'n' && scanner->current - scanner->start > 2) {
          switch (scanner->start[2]) {
            case 'd': return check_keyword(scanner, 3, 0, "", TOKEN_AND);
            case 'y': break;
            default: break;
          }
        }
      }
      break;
    case 'b': break;
    case 'c': return check_keyword(scanner, 1, 4, "lass", TOKEN_CLASS);
    case 'e':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 'l': return check_keyword(scanner, 2, 2, "se", TOKEN_ELSE);
          case 'x': return check_keyword(scanner, 2, 4, "tern", TOKEN_EXTERN);
          default: break;
        }
      }
      break;
    case 'f':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 'a': return check_keyword(scanner, 2, 3, "lse", TOKEN_FALSE);
          case 'o': return check_keyword(scanner, 2, 1, "r", TOKEN_FOR);
          case 'u': return check_keyword(scanner, 2, 1, "n", TOKEN_FUN);
          default: break;
        }
      }
      break;
    case 'i':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 'f': return check_keyword(scanner, 2, 0, "", TOKEN_IF);
          case 'm': return check_keyword(scanner, 2, 4, "port", TOKEN_IMPORT);
          default: break;
        }
      }
      break;
    case 'l': return check_keyword(scanner, 1, 2, "et", TOKEN_LET);
    case 'n':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 'i': return check_keyword(scanner, 2, 1, "l", TOKEN_NIL);
          case 'o': break;
          case 'u': break;
          default: break;
        }
      }
      break;
    case 'o': return check_keyword(scanner, 1, 1, "r", TOKEN_OR);
    case 'r': return check_keyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
    case 's':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 't': break;
          case 'u': return check_keyword(scanner, 2, 3, "per", TOKEN_SUPER);
          default: break;
        }
      }
      break;
    case 't':
      if (scanner->current - scanner->start > 1) {
        switch (scanner->start[1]) {
          case 'h': return check_keyword(scanner, 2, 2, "is", TOKEN_THIS);
          case 'r': return check_keyword(scanner, 2, 2, "ue", TOKEN_TRUE);
          default: break;
        }
      }
      break;
    case 'w': return check_keyword(scanner, 1, 4, "hile", TOKEN_WHILE);
    default: break;
  }
  return TOKEN_IDENTIFIER;
}

static srclang_token_t identifier(srclang_scanner_t* scanner) {
  while (is_alpha(peek(scanner)) || is_digit(peek(scanner))) advance(scanner);
  return make_token(scanner, identifier_type(scanner));
}

static srclang_token_t number(srclang_scanner_t* scanner) {
  while (is_digit(peek(scanner))) advance(scanner);
  if (peek(scanner) == '.' && is_digit(peek_next(scanner))) {
    advance(scanner);
    while (is_digit(peek(scanner))) advance(scanner);
  }
  return make_token(scanner, TOKEN_NUMBER);
}

static srclang_token_t string(srclang_scanner_t* scanner) {
  while (peek(scanner) != '"' && !is_at_end(scanner)) {
    if (peek(scanner) == '\\' && peek_next(scanner) != '\0') {
      advance(scanner);
    }
    advance(scanner);
  }

  if (is_at_end(scanner)) return error_token(scanner, "Unterminated string.");
  advance(scanner);
  return make_token(scanner, TOKEN_STRING);
}

srclang_token_t srclang_scan_token(srclang_scanner_t* scanner) {
  skip_whitespace(scanner);
  scanner->start = scanner->current;
  scanner->token_column = scanner->column;

  if (is_at_end(scanner)) return make_token(scanner, TOKEN_EOF);

  char c = advance(scanner);
  if (is_alpha(c)) return identifier(scanner);
  if (is_digit(c)) return number(scanner);

  switch (c) {
    case '(': return make_token(scanner, TOKEN_LEFT_PAREN);
    case ')': return make_token(scanner, TOKEN_RIGHT_PAREN);
    case '{': return make_token(scanner, TOKEN_LEFT_BRACE);
    case '}': return make_token(scanner, TOKEN_RIGHT_BRACE);
    case '[': return make_token(scanner, TOKEN_LEFT_BRACKET);
    case ']': return make_token(scanner, TOKEN_RIGHT_BRACKET);
    case ',': return make_token(scanner, TOKEN_COMMA);
    case '.':
      if (peek(scanner) == '.' && peek_next(scanner) == '.') {
        advance(scanner);
        advance(scanner);
        return make_token(scanner, TOKEN_ELLIPSIS);
      }
      return make_token(scanner, TOKEN_DOT);
    case '-': return make_token(scanner, TOKEN_MINUS);
    case '+': return make_token(scanner, TOKEN_PLUS);
    case ';': return make_token(scanner, TOKEN_SEMICOLON);
    case '*': return make_token(scanner, TOKEN_STAR);
    case '%': return make_token(scanner, TOKEN_PERCENT);
    case ':': return make_token(scanner, TOKEN_COLON);
    case '?': return make_token(scanner, TOKEN_QUESTION);
    case '!': return make_token(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=': return make_token(scanner, match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<': return make_token(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>': return make_token(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '/': return make_token(scanner, TOKEN_SLASH);
    case '"': return string(scanner);
    default: break;
  }

  return error_token(scanner, "Unexpected character.");
}

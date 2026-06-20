#ifndef SRCLANG_AST_H
#define SRCLANG_AST_H

#include "common.h"

typedef struct srclang_type_node_t srclang_type_node_t;
typedef struct srclang_expr_t srclang_expr_t;
typedef struct srclang_stmt_t srclang_stmt_t;

typedef enum {
  TYPE_BUILTIN,
  TYPE_NAME,
  TYPE_ARRAY,
  TYPE_NULLABLE,
  TYPE_GROUP,
  TYPE_FUNCTION
} srclang_type_kind_t;

typedef enum {
  BUILTIN_ANY,
  BUILTIN_NIL,
  BUILTIN_BOOL,
  BUILTIN_NUM,
  BUILTIN_U8,
  BUILTIN_U16,
  BUILTIN_U32,
  BUILTIN_U64,
  BUILTIN_I8,
  BUILTIN_I16,
  BUILTIN_I32,
  BUILTIN_I64,
  BUILTIN_F32,
  BUILTIN_F64,
  BUILTIN_STR,
  BUILTIN_STRING,
  BUILTIN_PTR,
  BUILTIN_NONE
} srclang_builtin_type_t;

struct srclang_type_node_t {
  srclang_type_kind_t kind;
  union {
    srclang_builtin_type_t builtin;
    char* name;
    srclang_type_node_t* inner;
    struct {
      srclang_type_node_t** params;
      int param_count;
      srclang_type_node_t* result;
    } function;
  } as;
};

typedef struct {
  char* name;
  srclang_type_node_t* type;
} srclang_param_t;

typedef enum {
  LITERAL_NIL,
  LITERAL_BOOL,
  LITERAL_NUMBER,
  LITERAL_STRING
} srclang_literal_kind_t;

typedef struct {
  srclang_literal_kind_t kind;
  union {
    bool boolean;
    double number;
    char* string;
  } as;
} srclang_literal_t;

typedef enum {
  EXPR_LITERAL,
  EXPR_VARIABLE,
  EXPR_ASSIGN,
  EXPR_BINARY,
  EXPR_LOGICAL,
  EXPR_UNARY,
  EXPR_CALL,
  EXPR_GET,
  EXPR_SET,
  EXPR_THIS,
  EXPR_SUPER,
  EXPR_GROUPING
} srclang_expr_kind_t;

struct srclang_expr_t {
  srclang_expr_kind_t kind;
  int line;
  union {
    srclang_literal_t literal;
    char* variable;
    struct {
      char* name;
      srclang_expr_t* value;
    } assign;
    struct {
      srclang_expr_t* left;
      int op;
      srclang_expr_t* right;
    } binary;
    struct {
      srclang_expr_t* left;
      int op;
      srclang_expr_t* right;
    } logical;
    struct {
      int op;
      srclang_expr_t* right;
    } unary;
    struct {
      srclang_expr_t* callee;
      srclang_expr_t** args;
      int arg_count;
    } call;
    struct {
      srclang_expr_t* object;
      char* name;
    } get;
    struct {
      srclang_expr_t* object;
      char* name;
      srclang_expr_t* value;
    } set;
    char* super_method;
    srclang_expr_t* grouping;
  } as;
};

typedef struct {
  char* name;
  srclang_param_t* params;
  int param_count;
  srclang_type_node_t* return_type;
  srclang_stmt_t* body;
  bool is_extern;
  bool is_variadic;
} srclang_function_decl_t;

typedef enum {
  CLASS_MEMBER_FIELD,
  CLASS_MEMBER_METHOD
} srclang_class_member_kind_t;

typedef struct {
  srclang_class_member_kind_t kind;
  int line;
  union {
    struct {
      char* name;
      srclang_type_node_t* type;
      srclang_expr_t* initializer;
    } field;
    struct {
      bool is_static;
      srclang_function_decl_t function;
    } method;
  } as;
} srclang_class_member_t;

typedef enum {
  STMT_EXPR,
  STMT_VAR,
  STMT_BLOCK,
  STMT_IF,
  STMT_WHILE,
  STMT_FOR,
  STMT_RETURN,
  STMT_FUNCTION,
  STMT_CLASS,
  STMT_IMPORT
} srclang_stmt_kind_t;

struct srclang_stmt_t {
  srclang_stmt_kind_t kind;
  int line;
  union {
    srclang_expr_t* expression;
    struct {
      char* name;
      srclang_type_node_t* type;
      srclang_expr_t* initializer;
    } var;
    struct {
      srclang_stmt_t** declarations;
      int count;
    } block;
    struct {
      srclang_expr_t* condition;
      srclang_stmt_t* then_branch;
      srclang_stmt_t* else_branch;
    } if_stmt;
    struct {
      srclang_expr_t* condition;
      srclang_stmt_t* body;
    } while_stmt;
    struct {
      srclang_stmt_t* initializer;
      srclang_expr_t* condition;
      srclang_expr_t* increment;
      srclang_stmt_t* body;
    } for_stmt;
    srclang_expr_t* return_value;
    srclang_function_decl_t function;
    struct {
      char* name;
      char* superclass;
      srclang_class_member_t* members;
      int member_count;
    } class_decl;
    char* import_path;
  } as;
};

typedef struct {
  srclang_stmt_t** declarations;
  int count;
} srclang_ast_program_t;

srclang_type_node_t* srclang_type_new(srclang_type_kind_t kind);
srclang_expr_t* srclang_expr_new(srclang_expr_kind_t kind, int line);
srclang_stmt_t* srclang_stmt_new(srclang_stmt_kind_t kind, int line);

void srclang_ast_program_init(srclang_ast_program_t* program);
void srclang_ast_program_write(srclang_ast_program_t* program, srclang_stmt_t* stmt);
void srclang_ast_program_free(srclang_ast_program_t* program);

char* srclang_ast_copy(const char* chars, int length);

#endif

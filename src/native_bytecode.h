#ifndef SRCLANG_NATIVE_BYTECODE_H
#define SRCLANG_NATIVE_BYTECODE_H

#include "ast.h"
#include "context.h"

typedef enum {
  SRCLANG_NATIVE_TYPE_NONE,
  SRCLANG_NATIVE_TYPE_NIL,
  SRCLANG_NATIVE_TYPE_BOOL,
  SRCLANG_NATIVE_TYPE_U8,
  SRCLANG_NATIVE_TYPE_U16,
  SRCLANG_NATIVE_TYPE_U32,
  SRCLANG_NATIVE_TYPE_U64,
  SRCLANG_NATIVE_TYPE_I8,
  SRCLANG_NATIVE_TYPE_I16,
  SRCLANG_NATIVE_TYPE_I32,
  SRCLANG_NATIVE_TYPE_I64,
  SRCLANG_NATIVE_TYPE_F32,
  SRCLANG_NATIVE_TYPE_F64,
  SRCLANG_NATIVE_TYPE_STR,
  SRCLANG_NATIVE_TYPE_STRING,
  SRCLANG_NATIVE_TYPE_PTR,
  SRCLANG_NATIVE_TYPE_ANY
} srclang_native_type_t;

typedef enum {
  SRCLANG_BC_PUSH_I64,
  SRCLANG_BC_PUSH_STR,
  SRCLANG_BC_LOAD_LOCAL,
  SRCLANG_BC_STORE_LOCAL,
  SRCLANG_BC_LOAD_GLOBAL,
  SRCLANG_BC_STORE_GLOBAL,
  SRCLANG_BC_POP,
  SRCLANG_BC_NEG_I64,
  SRCLANG_BC_NOT,
  SRCLANG_BC_ADD_I64,
  SRCLANG_BC_SUB_I64,
  SRCLANG_BC_MUL_I64,
  SRCLANG_BC_DIV_I64,
  SRCLANG_BC_MOD_I64,
  SRCLANG_BC_EQ_I64,
  SRCLANG_BC_NE_I64,
  SRCLANG_BC_GT_I64,
  SRCLANG_BC_GE_I64,
  SRCLANG_BC_LT_I64,
  SRCLANG_BC_LE_I64,
  SRCLANG_BC_JUMP,
  SRCLANG_BC_JUMP_IF_FALSE,
  SRCLANG_BC_CALL,
  SRCLANG_BC_INLINE_ASM,
  SRCLANG_BC_RETURN
} srclang_native_opcode_t;

typedef struct {
  srclang_native_opcode_t op;
  int line;
  int operand;
  int operand2;
  long long imm;
  char* symbol;
} srclang_native_instr_t;

typedef struct {
  char* name;
  srclang_native_type_t type;
  int slot;
} srclang_native_local_t;

typedef struct {
  char* name;
  srclang_native_type_t type;
  bool has_initializer;
  bool initializer_is_string;
  long long initializer_value;
  int initializer_string;
} srclang_native_global_t;

typedef struct {
  char* value;
  char* label;
} srclang_native_string_t;

typedef struct {
  char* name;
  char* symbol;
  srclang_native_type_t return_type;
  int arity;
  bool is_external;
  bool is_variadic;
  srclang_native_local_t* locals;
  int local_count;
  int local_capacity;
  srclang_native_instr_t* code;
  int code_count;
  int code_capacity;
} srclang_native_function_t;

typedef struct {
  char* module_name;
  srclang_native_global_t* globals;
  int global_count;
  int global_capacity;
  srclang_native_function_t* functions;
  int function_count;
  int function_capacity;
  srclang_native_string_t* strings;
  int string_count;
  int string_capacity;
} srclang_native_program_t;

void srclang_native_program_init(srclang_native_program_t* program, const char* module_name);
void srclang_native_program_free(srclang_native_program_t* program);

bool srclang_native_compile_program(
    srclang_context_t ctx,
    const srclang_ast_program_t* ast,
    const char* module_name,
    srclang_native_program_t* program);

bool srclang_native_compile_program_with_entry(
    srclang_context_t ctx,
    const srclang_ast_program_t* ast,
    const char* module_name,
    bool emit_entry,
    srclang_native_program_t* program);

char* srclang_native_bytecode_dump(srclang_context_t ctx, const srclang_native_program_t* program);

const char* srclang_native_type_name(srclang_native_type_t type);
char* srclang_native_mangle(const char* prefix, const char* name);

#endif

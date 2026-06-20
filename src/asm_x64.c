#include "asm_x64.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char* data;
  size_t count;
  size_t capacity;
} srclang_asm_writer_t;

static const char* arg_registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

static void writer_init(srclang_asm_writer_t* writer) {
  writer->data = NULL;
  writer->count = 0;
  writer->capacity = 0;
}

static void writer_reserve(srclang_asm_writer_t* writer, size_t additional) {
  if (writer->capacity >= writer->count + additional + 1) return;
  size_t capacity = writer->capacity < 1024 ? 1024 : writer->capacity * 2;
  while (capacity < writer->count + additional + 1) capacity *= 2;
  char* grown = (char*)realloc(writer->data, capacity);
  if (grown == NULL) abort();
  writer->data = grown;
  writer->capacity = capacity;
}

static void emit_text(srclang_asm_writer_t* writer, const char* text) {
  size_t length = strlen(text);
  writer_reserve(writer, length);
  memcpy(writer->data + writer->count, text, length);
  writer->count += length;
  writer->data[writer->count] = '\0';
}

static void emitf(srclang_asm_writer_t* writer, const char* format, ...) {
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

static void emit_escaped_string(srclang_asm_writer_t* writer, const char* value) {
  emit_text(writer, "\"");
  for (const unsigned char* c = (const unsigned char*)value; *c != '\0'; c++) {
    switch (*c) {
      case '\\': emit_text(writer, "\\\\"); break;
      case '"': emit_text(writer, "\\\""); break;
      case '\n': emit_text(writer, "\\n"); break;
      case '\r': emit_text(writer, "\\r"); break;
      case '\t': emit_text(writer, "\\t"); break;
      default:
        if (*c < 32 || *c > 126) emitf(writer, "\\%03o", (unsigned)*c);
        else emitf(writer, "%c", *c);
        break;
    }
  }
  emit_text(writer, "\"");
}

static int local_offset(int slot) {
  return (slot + 1) * 8;
}

static int frame_size(const srclang_native_function_t* function) {
  int raw = function->local_count * 8;
  int aligned = (raw + 15) & ~15;
  return aligned < 16 ? 16 : aligned;
}

static void emit_comparison(srclang_asm_writer_t* writer, const char* setcc) {
  emit_text(writer, "  pop rbx\n");
  emit_text(writer, "  pop rax\n");
  emit_text(writer, "  cmp rax, rbx\n");
  emitf(writer, "  %s al\n", setcc);
  emit_text(writer, "  movzx rax, al\n");
  emit_text(writer, "  push rax\n");
}

static void emit_inline_asm(srclang_asm_writer_t* writer, const char* source) {
  emit_text(writer, "  # srclang inline asm begin\n");
  if (source != NULL && source[0] != '\0') {
    emit_text(writer, source);
    if (source[strlen(source) - 1] != '\n') emit_text(writer, "\n");
  }
  emit_text(writer, "  # srclang inline asm end\n");
}

static void emit_instruction(
    srclang_asm_writer_t* writer,
    const srclang_native_program_t* program,
    const srclang_native_function_t* function,
    const srclang_native_instr_t* ins,
    int index) {
  switch (ins->op) {
    case SRCLANG_BC_PUSH_I64:
      emitf(writer, "  mov rax, %lld\n  push rax\n", ins->imm);
      break;
    case SRCLANG_BC_PUSH_STR:
      emitf(writer, "  lea rax, [rip + %s]\n  push rax\n", program->strings[ins->operand].label);
      break;
    case SRCLANG_BC_LOAD_LOCAL:
      emitf(writer, "  mov rax, qword ptr [rbp - %d]\n  push rax\n", local_offset(ins->operand));
      break;
    case SRCLANG_BC_STORE_LOCAL:
      emitf(writer, "  pop rax\n  mov qword ptr [rbp - %d], rax\n  push rax\n", local_offset(ins->operand));
      break;
    case SRCLANG_BC_LOAD_GLOBAL:
      emitf(writer, "  mov rax, qword ptr [rip + %s]\n  push rax\n", ins->symbol);
      break;
    case SRCLANG_BC_STORE_GLOBAL:
      emitf(writer, "  pop rax\n  mov qword ptr [rip + %s], rax\n  push rax\n", ins->symbol);
      break;
    case SRCLANG_BC_POP:
      emit_text(writer, "  add rsp, 8\n");
      break;
    case SRCLANG_BC_NEG_I64:
      emit_text(writer, "  pop rax\n  neg rax\n  push rax\n");
      break;
    case SRCLANG_BC_NOT:
      emit_text(writer, "  pop rax\n  cmp rax, 0\n  sete al\n  movzx rax, al\n  push rax\n");
      break;
    case SRCLANG_BC_ADD_I64:
      emit_text(writer, "  pop rbx\n  pop rax\n  add rax, rbx\n  push rax\n");
      break;
    case SRCLANG_BC_SUB_I64:
      emit_text(writer, "  pop rbx\n  pop rax\n  sub rax, rbx\n  push rax\n");
      break;
    case SRCLANG_BC_MUL_I64:
      emit_text(writer, "  pop rbx\n  pop rax\n  imul rax, rbx\n  push rax\n");
      break;
    case SRCLANG_BC_DIV_I64:
      emit_text(writer, "  pop rbx\n  pop rax\n  cqo\n  idiv rbx\n  push rax\n");
      break;
    case SRCLANG_BC_MOD_I64:
      emit_text(writer, "  pop rbx\n  pop rax\n  cqo\n  idiv rbx\n  push rdx\n");
      break;
    case SRCLANG_BC_EQ_I64: emit_comparison(writer, "sete"); break;
    case SRCLANG_BC_NE_I64: emit_comparison(writer, "setne"); break;
    case SRCLANG_BC_GT_I64: emit_comparison(writer, "setg"); break;
    case SRCLANG_BC_GE_I64: emit_comparison(writer, "setge"); break;
    case SRCLANG_BC_LT_I64: emit_comparison(writer, "setl"); break;
    case SRCLANG_BC_LE_I64: emit_comparison(writer, "setle"); break;
    case SRCLANG_BC_JUMP:
      emitf(writer, "  jmp .L_%s_%d\n", function->symbol, ins->operand);
      break;
    case SRCLANG_BC_JUMP_IF_FALSE:
      emit_text(writer, "  pop rax\n  cmp rax, 0\n");
      emitf(writer, "  je .L_%s_%d\n", function->symbol, ins->operand);
      break;
    case SRCLANG_BC_CALL:
      if (ins->operand > 6) {
        emit_text(writer, "  # too many arguments for current backend\n");
      }
      for (int i = ins->operand - 1; i >= 0 && i < 6; i--) {
        emitf(writer, "  pop %s\n", arg_registers[i]);
      }
      if (ins->operand2 != 0) emit_text(writer, "  xor eax, eax\n");
      emitf(writer, "  call %s\n  push rax\n", ins->symbol);
      break;
    case SRCLANG_BC_INLINE_ASM:
      emit_inline_asm(writer, ins->symbol);
      break;
    case SRCLANG_BC_RETURN:
      emit_text(writer, "  pop rax\n");
      emitf(writer, "  jmp .L_%s_return\n", function->symbol);
      break;
  }
  (void)index;
}

static void emit_function(srclang_asm_writer_t* writer, const srclang_native_program_t* program, const srclang_native_function_t* function) {
  emitf(writer, ".global %s\n%s:\n", function->symbol, function->symbol);
  emit_text(writer, "  push rbp\n  mov rbp, rsp\n");
  emitf(writer, "  sub rsp, %d\n", frame_size(function));
  for (int i = 0; i < function->arity && i < 6; i++) {
    emitf(writer, "  mov qword ptr [rbp - %d], %s\n", local_offset(i), arg_registers[i]);
  }
  for (int i = 0; i < function->code_count; i++) {
    emitf(writer, ".L_%s_%d:\n", function->symbol, i);
    emit_instruction(writer, program, function, &function->code[i], i);
  }
  emitf(writer, ".L_%s_%d:\n", function->symbol, function->code_count);
  emit_text(writer, "  xor rax, rax\n");
  emitf(writer, ".L_%s_return:\n", function->symbol);
  emit_text(writer, "  mov rsp, rbp\n  pop rbp\n  ret\n\n");
}

char* srclang_x64_emit_program(srclang_context_t ctx, const srclang_native_program_t* program) {
  (void)ctx;
  srclang_asm_writer_t writer;
  writer_init(&writer);
  emit_text(&writer, ".intel_syntax noprefix\n");
  emit_text(&writer, ".section .rodata\n");
  for (int i = 0; i < program->string_count; i++) {
    emitf(&writer, "%s:\n  .asciz ", program->strings[i].label);
    emit_escaped_string(&writer, program->strings[i].value);
    emit_text(&writer, "\n");
  }
  emit_text(&writer, "\n.data\n");
  for (int i = 0; i < program->global_count; i++) {
    char* symbol = srclang_native_mangle("srclang_global_", program->globals[i].name);
    emitf(&writer, ".global %s\n.hidden %s\n%s:\n  .quad ", symbol, symbol, symbol);
    if (program->globals[i].has_initializer && program->globals[i].initializer_is_string) {
      emitf(&writer, "%s", program->strings[program->globals[i].initializer_string].label);
    } else if (program->globals[i].has_initializer) {
      emitf(&writer, "%lld", program->globals[i].initializer_value);
    } else {
      emit_text(&writer, "0");
    }
    emit_text(&writer, "\n");
    free(symbol);
  }
  emit_text(&writer, "\n.text\n");
  for (int i = 0; i < program->function_count; i++) {
    if (program->functions[i].is_external) {
      emitf(&writer, ".extern %s\n", program->functions[i].symbol);
      continue;
    }
    emit_function(&writer, program, &program->functions[i]);
  }
  emit_text(&writer, ".section .note.GNU-stack,\"\",@progbits\n");
  return writer.data;
}

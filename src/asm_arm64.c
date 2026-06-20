#include "asm_arm64.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char* data;
  size_t count;
  size_t capacity;
} srclang_arm64_writer_t;

static const char* arg_registers[] = {"x0", "x1", "x2", "x3", "x4", "x5"};

static void writer_init(srclang_arm64_writer_t* writer) {
  writer->data = NULL;
  writer->count = 0;
  writer->capacity = 0;
}

static void writer_reserve(srclang_arm64_writer_t* writer, size_t additional) {
  if (writer->capacity >= writer->count + additional + 1) return;
  size_t capacity = writer->capacity < 1024 ? 1024 : writer->capacity * 2;
  while (capacity < writer->count + additional + 1) capacity *= 2;
  char* grown = (char*)realloc(writer->data, capacity);
  if (grown == NULL) abort();
  writer->data = grown;
  writer->capacity = capacity;
}

static void emit_text(srclang_arm64_writer_t* writer, const char* text) {
  size_t length = strlen(text);
  writer_reserve(writer, length);
  memcpy(writer->data + writer->count, text, length);
  writer->count += length;
  writer->data[writer->count] = '\0';
}

static void emitf(srclang_arm64_writer_t* writer, const char* format, ...) {
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

static void emit_escaped_string(srclang_arm64_writer_t* writer, const char* value) {
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

static void emit_push(srclang_arm64_writer_t* writer, const char* reg) {
  emitf(writer, "  str %s, [sp, #-16]!\n", reg);
}

static void emit_pop(srclang_arm64_writer_t* writer, const char* reg) {
  emitf(writer, "  ldr %s, [sp], #16\n", reg);
}

static void emit_load_imm64(srclang_arm64_writer_t* writer, const char* reg, long long value) {
  unsigned long long bits = (unsigned long long)value;
  emitf(writer, "  movz %s, #%llu\n", reg, bits & 0xffffULL);
  for (int shift = 16; shift < 64; shift += 16) {
    unsigned long long part = (bits >> shift) & 0xffffULL;
    if (part != 0) emitf(writer, "  movk %s, #%llu, lsl #%d\n", reg, part, shift);
  }
}

static void emit_sub_immediate_or_register(
    srclang_arm64_writer_t* writer,
    const char* dst,
    const char* left,
    int value) {
  if (value >= 0 && value <= 4095) {
    emitf(writer, "  sub %s, %s, #%d\n", dst, left, value);
    return;
  }

  emit_load_imm64(writer, "x10", value);
  emitf(writer, "  sub %s, %s, x10\n", dst, left);
}

static void emit_load_symbol_address(srclang_arm64_writer_t* writer, const char* reg, const char* symbol) {
  emitf(writer, "  adrp %s, %s\n", reg, symbol);
  emitf(writer, "  add %s, %s, :lo12:%s\n", reg, reg, symbol);
}

static void emit_load_local(srclang_arm64_writer_t* writer, int slot) {
  emit_sub_immediate_or_register(writer, "x9", "x29", local_offset(slot));
  emit_text(writer, "  ldr x0, [x9]\n");
  emit_push(writer, "x0");
}

static void emit_store_local(srclang_arm64_writer_t* writer, int slot) {
  emit_pop(writer, "x0");
  emit_sub_immediate_or_register(writer, "x9", "x29", local_offset(slot));
  emit_text(writer, "  str x0, [x9]\n");
  emit_push(writer, "x0");
}

static void emit_comparison(srclang_arm64_writer_t* writer, const char* condition) {
  emit_pop(writer, "x1");
  emit_pop(writer, "x0");
  emit_text(writer, "  cmp x0, x1\n");
  emitf(writer, "  cset x0, %s\n", condition);
  emit_push(writer, "x0");
}

static void emit_inline_asm(srclang_arm64_writer_t* writer, const char* source) {
  emit_text(writer, "  // srclang inline asm begin\n");
  if (source != NULL && source[0] != '\0') {
    emit_text(writer, source);
    if (source[strlen(source) - 1] != '\n') emit_text(writer, "\n");
  }
  emit_text(writer, "  // srclang inline asm end\n");
}

static void emit_instruction(
    srclang_arm64_writer_t* writer,
    const srclang_native_program_t* program,
    const srclang_native_function_t* function,
    const srclang_native_instr_t* ins,
    int index) {
  switch (ins->op) {
    case SRCLANG_BC_PUSH_I64:
      emit_load_imm64(writer, "x0", ins->imm);
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_PUSH_STR:
      emit_load_symbol_address(writer, "x0", program->strings[ins->operand].label);
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_LOAD_LOCAL:
      emit_load_local(writer, ins->operand);
      break;
    case SRCLANG_BC_STORE_LOCAL:
      emit_store_local(writer, ins->operand);
      break;
    case SRCLANG_BC_LOAD_GLOBAL:
      emit_load_symbol_address(writer, "x9", ins->symbol);
      emit_text(writer, "  ldr x0, [x9]\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_STORE_GLOBAL:
      emit_pop(writer, "x0");
      emit_load_symbol_address(writer, "x9", ins->symbol);
      emit_text(writer, "  str x0, [x9]\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_POP:
      emit_text(writer, "  add sp, sp, #16\n");
      break;
    case SRCLANG_BC_NEG_I64:
      emit_pop(writer, "x0");
      emit_text(writer, "  neg x0, x0\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_NOT:
      emit_pop(writer, "x0");
      emit_text(writer, "  cmp x0, #0\n  cset x0, eq\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_ADD_I64:
      emit_pop(writer, "x1");
      emit_pop(writer, "x0");
      emit_text(writer, "  add x0, x0, x1\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_SUB_I64:
      emit_pop(writer, "x1");
      emit_pop(writer, "x0");
      emit_text(writer, "  sub x0, x0, x1\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_MUL_I64:
      emit_pop(writer, "x1");
      emit_pop(writer, "x0");
      emit_text(writer, "  mul x0, x0, x1\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_DIV_I64:
      emit_pop(writer, "x1");
      emit_pop(writer, "x0");
      emit_text(writer, "  sdiv x0, x0, x1\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_MOD_I64:
      emit_pop(writer, "x1");
      emit_pop(writer, "x0");
      emit_text(writer, "  sdiv x2, x0, x1\n  msub x0, x2, x1, x0\n");
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_EQ_I64: emit_comparison(writer, "eq"); break;
    case SRCLANG_BC_NE_I64: emit_comparison(writer, "ne"); break;
    case SRCLANG_BC_GT_I64: emit_comparison(writer, "gt"); break;
    case SRCLANG_BC_GE_I64: emit_comparison(writer, "ge"); break;
    case SRCLANG_BC_LT_I64: emit_comparison(writer, "lt"); break;
    case SRCLANG_BC_LE_I64: emit_comparison(writer, "le"); break;
    case SRCLANG_BC_JUMP:
      emitf(writer, "  b .L_%s_%d\n", function->symbol, ins->operand);
      break;
    case SRCLANG_BC_JUMP_IF_FALSE:
      emit_pop(writer, "x0");
      emit_text(writer, "  cmp x0, #0\n");
      emitf(writer, "  b.eq .L_%s_%d\n", function->symbol, ins->operand);
      break;
    case SRCLANG_BC_CALL:
      if (ins->operand > 6) {
        emit_text(writer, "  // too many arguments for current backend\n");
      }
      for (int i = ins->operand - 1; i >= 0 && i < 6; i--) {
        emit_pop(writer, arg_registers[i]);
      }
      emitf(writer, "  bl %s\n", ins->symbol);
      emit_push(writer, "x0");
      break;
    case SRCLANG_BC_INLINE_ASM:
      emit_inline_asm(writer, ins->symbol);
      break;
    case SRCLANG_BC_RETURN:
      emit_pop(writer, "x0");
      emitf(writer, "  b .L_%s_return\n", function->symbol);
      break;
  }
  (void)index;
}

static void emit_function(
    srclang_arm64_writer_t* writer,
    const srclang_native_program_t* program,
    const srclang_native_function_t* function) {
  emitf(writer, ".global %s\n.type %s, %%function\n%s:\n", function->symbol, function->symbol, function->symbol);
  emit_text(writer, "  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n");
  emit_sub_immediate_or_register(writer, "sp", "sp", frame_size(function));
  for (int i = 0; i < function->arity && i < 6; i++) {
    emit_sub_immediate_or_register(writer, "x9", "x29", local_offset(i));
    emitf(writer, "  str %s, [x9]\n", arg_registers[i]);
  }
  for (int i = 0; i < function->code_count; i++) {
    emitf(writer, ".L_%s_%d:\n", function->symbol, i);
    emit_instruction(writer, program, function, &function->code[i], i);
  }
  emitf(writer, ".L_%s_%d:\n", function->symbol, function->code_count);
  emit_text(writer, "  mov x0, #0\n");
  emitf(writer, ".L_%s_return:\n", function->symbol);
  emit_text(writer, "  mov sp, x29\n  ldp x29, x30, [sp], #16\n  ret\n");
  emitf(writer, ".size %s, .-%s\n\n", function->symbol, function->symbol);
}

char* srclang_arm64_emit_program(srclang_context_t ctx, const srclang_native_program_t* program) {
  (void)ctx;
  srclang_arm64_writer_t writer;
  writer_init(&writer);

  emit_text(&writer, ".arch armv8-a\n");
  emit_text(&writer, ".section .rodata\n");
  for (int i = 0; i < program->string_count; i++) {
    emitf(&writer, "%s:\n  .asciz ", program->strings[i].label);
    emit_escaped_string(&writer, program->strings[i].value);
    emit_text(&writer, "\n");
  }

  emit_text(&writer, "\n.data\n.align 3\n");
  for (int i = 0; i < program->global_count; i++) {
    char* symbol = srclang_native_mangle("srclang_global_", program->globals[i].name);
    emitf(&writer, ".global %s\n.hidden %s\n.type %s, %%object\n.size %s, 8\n%s:\n  .quad ",
        symbol,
        symbol,
        symbol,
        symbol,
        symbol);
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

  emit_text(&writer, "\n.text\n.align 2\n");
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

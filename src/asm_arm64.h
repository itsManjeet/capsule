#ifndef SRCLANG_ASM_ARM64_H
#define SRCLANG_ASM_ARM64_H

#include "native_bytecode.h"

char* srclang_arm64_emit_program(srclang_context_t ctx, const srclang_native_program_t* program);

#endif

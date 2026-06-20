#ifndef SRCLANG_MODULE_H
#define SRCLANG_MODULE_H

#include "ast.h"
#include "context.h"

bool srclang_parse_source_with_imports(
    srclang_context_t ctx,
    const char* source,
    const char* origin_path,
    srclang_ast_program_t* program);

#endif

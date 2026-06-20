#ifndef SRCLANG_PARSER_H
#define SRCLANG_PARSER_H

#include "ast.h"
#include "context.h"

bool srclang_parse_source(srclang_context_t ctx, const char* source, srclang_ast_program_t* program);

#endif

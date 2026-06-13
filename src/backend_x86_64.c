#include "lipi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    LipiTypeKind type;
    int offset;
} CgLocal;

typedef struct {
    char *name;
    int label;
    LipiAst *bindings;
} CgNamedLet;

typedef struct X86 X86;

typedef struct {
    X86 *x;
    LipiCompiler *c;
    LipiVec locals; /* CgLocal* */
    LipiVec named_lets; /* CgNamedLet* */
    int stack_size;
    int push_depth;
} CgEnv;

struct X86 {
    LipiCompiler *c;
    LipiStr text;
    LipiStr rodata;
    LipiStr data;
    int label_id;
};

static const char *arg_regs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

static int gen_expr(CgEnv *env, LipiAst *expr);

static int is_head(LipiAst *node, const char *name) {
    return node && node->kind == AST_LIST && node->as.items.len > 0 &&
           lipi_ast_is_symbol((LipiAst *)node->as.items.items[0], name);
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int is_entry_test_function(LipiCompiler *c, LipiFunction *fn) {
    if (!starts_with(fn->name, "test-")) {
        return 0;
    }
    if (c->entry_file && fn->span.file && strcmp(c->entry_file, fn->span.file) != 0) {
        return 0;
    }
    if (c->test_filter && strcmp(c->test_filter, fn->name) != 0) {
        return 0;
    }
    return 1;
}

static int is_inline_mode(LipiAst *node, const char *mode) {
    return node && node->kind == AST_LIST && node->as.items.len == 3 &&
           lipi_ast_is_symbol((LipiAst *)node->as.items.items[0], "#inline") &&
           lipi_ast_is_symbol((LipiAst *)node->as.items.items[1], mode) &&
           ((LipiAst *)node->as.items.items[2])->kind == AST_BLOCK;
}

static char *typed_name_only(LipiCompiler *c, LipiAst *node) {
    char *name = NULL;
    char *type = NULL;
    if (!node || node->kind != AST_SYMBOL || !lipi_split_typed_name(c, node->as.text, &name, &type)) {
        return NULL;
    }
    return name;
}

static char *binding_name_only(LipiCompiler *c, LipiAst *node) {
    char *name = typed_name_only(c, node);
    if (name) {
        return name;
    }
    if (node && node->kind == AST_SYMBOL) {
        return node->as.text;
    }
    return NULL;
}

static CgLocal *local_find(CgEnv *env, const char *name) {
    for (int i = env->locals.len - 1; i >= 0; i--) {
        CgLocal *local = (CgLocal *)env->locals.items[i];
        if (strcmp(local->name, name) == 0) {
            return local;
        }
    }
    return NULL;
}

static CgNamedLet *named_let_find(CgEnv *env, const char *name) {
    for (int i = env->named_lets.len - 1; i >= 0; i--) {
        CgNamedLet *info = (CgNamedLet *)env->named_lets.items[i];
        if (strcmp(info->name, name) == 0) {
            return info;
        }
    }
    return NULL;
}

static void add_local(CgEnv *env, const char *name, LipiTypeKind type) {
    if (local_find(env, name)) {
        return;
    }
    CgLocal *local = (CgLocal *)lipi_arena_alloc(&env->c->arena, sizeof(CgLocal));
    local->name = (char *)name;
    local->type = type;
    local->offset = -8 * (env->locals.len + 1);
    lipi_vec_push(&env->locals, local);
}

static char *internal_local_name(LipiCompiler *c, LipiAst *node, const char *kind) {
    LipiStr s = {0};
    lipi_str_appendf(&s, "__lipi_%s_%d_%d", kind, node->span.line, node->span.column);
    char *name = lipi_arena_strdup(&c->arena, s.data ? s.data : kind);
    lipi_str_free(&s);
    return name;
}

static void scan_locals(CgEnv *env, LipiAst *node) {
    if (!node) return;
    if (is_head(node, "define") && node->as.items.len == 3 &&
        ((LipiAst *)node->as.items.items[1])->kind == AST_SYMBOL) {
        LipiAst *target = (LipiAst *)node->as.items.items[1];
        char *name = typed_name_only(env->c, target);
        if (name) {
            add_local(env, name, node->type);
        }
        scan_locals(env, (LipiAst *)node->as.items.items[2]);
        return;
    }
    if (is_head(node, "let") && node->as.items.len >= 3) {
        LipiAst *bindings = NULL;
        int body_start = 2;
        if (((LipiAst *)node->as.items.items[1])->kind == AST_LIST) {
            bindings = (LipiAst *)node->as.items.items[1];
        } else if (node->as.items.len >= 4 &&
                   ((LipiAst *)node->as.items.items[1])->kind == AST_SYMBOL &&
                   ((LipiAst *)node->as.items.items[2])->kind == AST_LIST) {
            bindings = (LipiAst *)node->as.items.items[2];
            body_start = 3;
        }
        if (!bindings) {
            return;
        }
        for (int i = 0; i < bindings->as.items.len; i++) {
            LipiAst *binding = (LipiAst *)bindings->as.items.items[i];
            if (binding->kind == AST_LIST && binding->as.items.len == 2) {
                char *name = binding_name_only(env->c, (LipiAst *)binding->as.items.items[0]);
                if (name) {
                    add_local(env, name, TYPE_UNKNOWN);
                }
                scan_locals(env, (LipiAst *)binding->as.items.items[1]);
            }
        }
        for (int i = body_start; i < node->as.items.len; i++) {
            scan_locals(env, (LipiAst *)node->as.items.items[i]);
        }
        return;
    }
    if (is_head(node, "for-each") && node->as.items.len >= 4 &&
        ((LipiAst *)node->as.items.items[1])->kind == AST_SYMBOL) {
        add_local(env, ((LipiAst *)node->as.items.items[1])->as.text, TYPE_UNKNOWN);
        add_local(env, internal_local_name(env->c, node, "foreach_idx"), TYPE_I64);
        add_local(env, internal_local_name(env->c, node, "foreach_collection"), TYPE_UNKNOWN);
        scan_locals(env, (LipiAst *)node->as.items.items[2]);
        for (int i = 3; i < node->as.items.len; i++) {
            scan_locals(env, (LipiAst *)node->as.items.items[i]);
        }
        return;
    }
    if (node->kind == AST_LIST || node->kind == AST_ARRAY || node->kind == AST_MAP) {
        for (int i = 0; i < node->as.items.len; i++) {
            scan_locals(env, (LipiAst *)node->as.items.items[i]);
        }
    }
}

static int round16(int n) {
    return (n + 15) & ~15;
}

static void finish_env_layout(CgEnv *env) {
    env->stack_size = round16(env->locals.len * 8);
}

static int new_label(X86 *x) {
    return ++x->label_id;
}

static void append_asm_string(LipiStr *out, const char *s) {
    lipi_str_append(out, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char ch = *p;
        switch (ch) {
        case '\\': lipi_str_append(out, "\\\\"); break;
        case '"': lipi_str_append(out, "\\\""); break;
        case '\n': lipi_str_append(out, "\\n"); break;
        case '\t': lipi_str_append(out, "\\t"); break;
        case '\r': lipi_str_append(out, "\\r"); break;
        default:
            if (ch < 32 || ch >= 127) {
                lipi_str_appendf(out, "\\%03o", ch);
            } else {
                char c = (char)ch;
                lipi_str_append_n(out, &c, 1);
            }
            break;
        }
    }
    lipi_str_append(out, "\"");
}

static void emit_builtin_helpers(X86 *x) {
    lipi_str_append(&x->text,
        "\n# lipi collection helper routines\n"
        ".Llipi_builtin_alloc:\n"
        "  push %rbp\n"
        "  mov %rsp, %rbp\n"
        "  mov %rdi, %rsi\n"
        "  xor %rdi, %rdi\n"
        "  mov $3, %rdx\n"
        "  mov $34, %r10\n"
        "  mov $-1, %r8\n"
        "  xor %r9, %r9\n"
        "  mov $9, %rax\n"
        "  syscall\n"
        "  cmp $-4095, %rax\n"
        "  jae .Llipi_builtin_alloc_fail\n"
        "  leave\n"
        "  ret\n"
        ".Llipi_builtin_alloc_fail:\n"
        "  xor %rax, %rax\n"
        "  leave\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_strlen:\n"
        "  xor %rax, %rax\n"
        ".Llipi_builtin_strlen_loop:\n"
        "  cmpb $0, (%rdi,%rax,1)\n"
        "  je .Llipi_builtin_strlen_done\n"
        "  inc %rax\n"
        "  jmp .Llipi_builtin_strlen_loop\n"
        ".Llipi_builtin_strlen_done:\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_streq:\n"
        "  xor %rax, %rax\n"
        ".Llipi_builtin_streq_loop:\n"
        "  movzbq (%rdi,%rax,1), %rcx\n"
        "  movzbq (%rsi,%rax,1), %rdx\n"
        "  cmp %rdx, %rcx\n"
        "  jne .Llipi_builtin_streq_false\n"
        "  test %rcx, %rcx\n"
        "  je .Llipi_builtin_streq_true\n"
        "  inc %rax\n"
        "  jmp .Llipi_builtin_streq_loop\n"
        ".Llipi_builtin_streq_true:\n"
        "  mov $1, %rax\n"
        "  ret\n"
        ".Llipi_builtin_streq_false:\n"
        "  xor %rax, %rax\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_string_concat:\n"
        "  push %rbp\n"
        "  mov %rsp, %rbp\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov %rdi, %r12\n"
        "  mov %rsi, %r13\n"
        "  mov %r12, %rdi\n"
        "  call .Llipi_builtin_strlen\n"
        "  mov %rax, %r14\n"
        "  mov %r13, %rdi\n"
        "  call .Llipi_builtin_strlen\n"
        "  mov %rax, %r15\n"
        "  lea 1(%r14,%r15,1), %rdi\n"
        "  call .Llipi_builtin_alloc\n"
        "  mov %rax, %rdx\n"
        "  xor %rcx, %rcx\n"
        ".Llipi_builtin_string_concat_copy_a:\n"
        "  cmp %r14, %rcx\n"
        "  jge .Llipi_builtin_string_concat_copy_b_start\n"
        "  movzbq (%r12,%rcx,1), %r8\n"
        "  movb %r8b, (%rdx,%rcx,1)\n"
        "  inc %rcx\n"
        "  jmp .Llipi_builtin_string_concat_copy_a\n"
        ".Llipi_builtin_string_concat_copy_b_start:\n"
        "  xor %r8, %r8\n"
        ".Llipi_builtin_string_concat_copy_b:\n"
        "  cmp %r15, %r8\n"
        "  jge .Llipi_builtin_string_concat_done\n"
        "  movzbq (%r13,%r8,1), %r9\n"
        "  movb %r9b, (%rdx,%rcx,1)\n"
        "  inc %r8\n"
        "  inc %rcx\n"
        "  jmp .Llipi_builtin_string_concat_copy_b\n"
        ".Llipi_builtin_string_concat_done:\n"
        "  movb $0, (%rdx,%rcx,1)\n"
        "  mov %rdx, %rax\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  leave\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_string_append_char:\n"
        "  push %rbp\n"
        "  mov %rsp, %rbp\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov %rdi, %r12\n"
        "  mov %rsi, %r13\n"
        "  mov %r12, %rdi\n"
        "  call .Llipi_builtin_strlen\n"
        "  mov %rax, %r14\n"
        "  lea 2(%r14), %rdi\n"
        "  call .Llipi_builtin_alloc\n"
        "  mov %rax, %rdx\n"
        "  xor %rcx, %rcx\n"
        ".Llipi_builtin_string_append_char_copy:\n"
        "  cmp %r14, %rcx\n"
        "  jge .Llipi_builtin_string_append_char_done\n"
        "  movzbq (%r12,%rcx,1), %r8\n"
        "  movb %r8b, (%rdx,%rcx,1)\n"
        "  inc %rcx\n"
        "  jmp .Llipi_builtin_string_append_char_copy\n"
        ".Llipi_builtin_string_append_char_done:\n"
        "  movb %r13b, (%rdx,%rcx,1)\n"
        "  inc %rcx\n"
        "  movb $0, (%rdx,%rcx,1)\n"
        "  mov %rdx, %rax\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  leave\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_array_append:\n"
        "  push %rbp\n"
        "  mov %rsp, %rbp\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov %rdi, %r12\n"
        "  mov %rsi, %r13\n"
        "  mov (%r12), %r14\n"
        "  lea 1(%r14), %r15\n"
        "  lea 16(,%r15,8), %rdi\n"
        "  call .Llipi_builtin_alloc\n"
        "  mov %rax, %rdx\n"
        "  mov %r15, (%rdx)\n"
        "  lea 16(%rdx), %r8\n"
        "  mov %r8, 8(%rdx)\n"
        "  mov 8(%r12), %r9\n"
        "  xor %rcx, %rcx\n"
        ".Llipi_builtin_array_append_copy:\n"
        "  cmp %r14, %rcx\n"
        "  jge .Llipi_builtin_array_append_store\n"
        "  mov (%r9,%rcx,8), %r10\n"
        "  mov %r10, (%r8,%rcx,8)\n"
        "  inc %rcx\n"
        "  jmp .Llipi_builtin_array_append_copy\n"
        ".Llipi_builtin_array_append_store:\n"
        "  mov %r13, (%r8,%r14,8)\n"
        "  mov %rdx, %rax\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  leave\n"
        "  ret\n"
        "\n"
        ".Llipi_builtin_map_get:\n"
        "  push %rbp\n"
        "  mov %rsp, %rbp\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov 8(%rdi), %r12\n"
        "  mov %rsi, %r13\n"
        "  xor %r14, %r14\n"
        "  mov (%rdi), %r15\n"
        ".Llipi_builtin_map_get_loop:\n"
        "  cmp %r15, %r14\n"
        "  jge .Llipi_builtin_map_get_missing\n"
        "  mov %r14, %rcx\n"
        "  imul $16, %rcx, %rcx\n"
        "  mov (%r12,%rcx,1), %rdi\n"
        "  mov %r13, %rsi\n"
        "  call .Llipi_builtin_streq\n"
        "  cmp $0, %rax\n"
        "  jne .Llipi_builtin_map_get_found\n"
        "  inc %r14\n"
        "  jmp .Llipi_builtin_map_get_loop\n"
        ".Llipi_builtin_map_get_found:\n"
        "  mov %r14, %rcx\n"
        "  imul $16, %rcx, %rcx\n"
        "  mov 8(%r12,%rcx,1), %rax\n"
        "  jmp .Llipi_builtin_map_get_done\n"
        ".Llipi_builtin_map_get_missing:\n"
        "  xor %rax, %rax\n"
        ".Llipi_builtin_map_get_done:\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  leave\n"
        "  ret\n"
    );
}

static const char *string_label(CgEnv *env, const char *value) {
    int id = new_label(env->x);
    char label[64];
    snprintf(label, sizeof(label), ".Lstr%d", id);
    lipi_str_appendf(&env->x->rodata, "%s:\n  .asciz ", label);
    append_asm_string(&env->x->rodata, value);
    lipi_str_append(&env->x->rodata, "\n");
    return lipi_arena_strdup(&env->c->arena, label);
}

static LipiSymbol *find_sym(LipiCompiler *c, const char *name) {
    return lipi_symbol_find(c, name);
}

static const char *symbol_label(CgEnv *env, LipiSymbol *sym) {
    if (sym->kind == SYM_EXTERN_FUNCTION || sym->kind == SYM_EXTERN_GLOBAL) {
        return sym->name;
    }
    return lipi_mangle(&env->c->arena, sym->name);
}

static int gen_symbol(CgEnv *env, LipiAst *expr) {
    if (strcmp(expr->as.text, "none") == 0 || strcmp(expr->as.text, "void") == 0) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    }
    CgLocal *local = local_find(env, expr->as.text);
    if (local) {
        lipi_str_appendf(&env->x->text, "  mov %d(%%rbp), %%rax\n", local->offset);
        return 1;
    }
    LipiSymbol *sym = find_sym(env->c, expr->as.text);
    if (!sym) {
        lipi_diag_error(env->c, expr->span, "backend could not resolve symbol", expr->as.text);
        return 0;
    }
    const char *label = symbol_label(env, sym);
    lipi_str_appendf(&env->x->text, "  mov %s(%%rip), %%rax\n", label);
    return 1;
}

static void push_rax(CgEnv *env) {
    lipi_str_append(&env->x->text, "  push %rax\n");
    env->push_depth++;
}

static void pop_reg(CgEnv *env, const char *reg) {
    lipi_str_appendf(&env->x->text, "  pop %s\n", reg);
    env->push_depth--;
}

static void gen_exit_code(CgEnv *env, int code) {
    lipi_str_appendf(&env->x->text, "  mov $%d, %%rdi\n  mov $60, %%rax\n  syscall\n", code);
}

static int gen_binary(CgEnv *env, LipiAst *expr, const char *op) {
    LipiAst *a = (LipiAst *)expr->as.items.items[1];
    LipiAst *b = (LipiAst *)expr->as.items.items[2];
    if (!gen_expr(env, a)) return 0;
    push_rax(env);
    if (!gen_expr(env, b)) return 0;
    pop_reg(env, "%rcx");

    int unsigned_op = lipi_type_is_unsigned_integer(a->type);
    if (strcmp(op, "+") == 0) {
        lipi_str_append(&env->x->text, "  add %rcx, %rax\n");
    } else if (strcmp(op, "-") == 0) {
        lipi_str_append(&env->x->text, "  sub %rax, %rcx\n  mov %rcx, %rax\n");
    } else if (strcmp(op, "*") == 0) {
        lipi_str_append(&env->x->text, "  imul %rcx, %rax\n");
    } else if (strcmp(op, "/") == 0) {
        if (unsigned_op) {
            lipi_str_append(&env->x->text, "  mov %rax, %r10\n  mov %rcx, %rax\n  xor %rdx, %rdx\n  div %r10\n");
        } else {
            lipi_str_append(&env->x->text, "  mov %rax, %r10\n  mov %rcx, %rax\n  cqto\n  idiv %r10\n");
        }
    } else {
        lipi_str_append(&env->x->text, "  cmp %rax, %rcx\n");
        if (strcmp(op, "=") == 0) {
            lipi_str_append(&env->x->text, "  sete %al\n");
        } else if (strcmp(op, "<") == 0) {
            lipi_str_append(&env->x->text, unsigned_op ? "  setb %al\n" : "  setl %al\n");
        } else if (strcmp(op, ">") == 0) {
            lipi_str_append(&env->x->text, unsigned_op ? "  seta %al\n" : "  setg %al\n");
        }
        lipi_str_append(&env->x->text, "  movzbq %al, %rax\n");
        return 1;
    }
    switch (expr->type) {
    case TYPE_I8: lipi_str_append(&env->x->text, "  movsbq %al, %rax\n"); break;
    case TYPE_U8: lipi_str_append(&env->x->text, "  movzbq %al, %rax\n"); break;
    case TYPE_I16: lipi_str_append(&env->x->text, "  movswq %ax, %rax\n"); break;
    case TYPE_U16: lipi_str_append(&env->x->text, "  movzwq %ax, %rax\n"); break;
    case TYPE_I32: lipi_str_append(&env->x->text, "  movslq %eax, %rax\n"); break;
    case TYPE_U32: lipi_str_append(&env->x->text, "  mov %eax, %eax\n"); break;
    default: break;
    }
    return 1;
}

static LipiTypeKind cast_target_from_name(const char *name) {
    LipiTypeKind direct = lipi_type_from_name(name);
    if (direct != TYPE_UNKNOWN && direct != TYPE_VOID) {
        return direct;
    }
    const char *arrow = strstr(name, "->");
    if (!arrow || arrow[2] == 0) {
        return TYPE_UNKNOWN;
    }
    char dst[64];
    size_t len = strlen(arrow + 2);
    if (len >= sizeof(dst)) {
        return TYPE_UNKNOWN;
    }
    memcpy(dst, arrow + 2, len + 1);
    LipiTypeKind target = lipi_type_from_name(dst);
    return target == TYPE_VOID ? TYPE_UNKNOWN : target;
}

static void apply_cast_result(CgEnv *env, LipiTypeKind target) {
    switch (target) {
    case TYPE_I8:
        lipi_str_append(&env->x->text, "  movsbq %al, %rax\n");
        break;
    case TYPE_U8:
        lipi_str_append(&env->x->text, "  movzbq %al, %rax\n");
        break;
    case TYPE_I16:
        lipi_str_append(&env->x->text, "  movswq %ax, %rax\n");
        break;
    case TYPE_U16:
        lipi_str_append(&env->x->text, "  movzwq %ax, %rax\n");
        break;
    case TYPE_I32:
        lipi_str_append(&env->x->text, "  movslq %eax, %rax\n");
        break;
    case TYPE_U32:
        lipi_str_append(&env->x->text, "  mov %eax, %eax\n");
        break;
    case TYPE_BOOL:
        lipi_str_append(&env->x->text, "  cmp $0, %rax\n  setne %al\n  movzbq %al, %rax\n");
        break;
    default:
        break;
    }
}

static int gen_cast(CgEnv *env, LipiAst *expr, LipiTypeKind target) {
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[1])) return 0;
    apply_cast_result(env, target);
    return 1;
}

static int gen_if(CgEnv *env, LipiAst *expr) {
    int id = new_label(env->x);
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[1])) return 0;
    lipi_str_append(&env->x->text, "  cmp $0, %rax\n");
    lipi_str_appendf(&env->x->text, "  je .Lelse%d\n", id);
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[2])) return 0;
    if (((LipiAst *)expr->as.items.items[2])->type == TYPE_VOID) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    }
    lipi_str_appendf(&env->x->text, "  jmp .Lendif%d\n", id);
    lipi_str_appendf(&env->x->text, ".Lelse%d:\n", id);
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[3])) return 0;
    if (((LipiAst *)expr->as.items.items[3])->type == TYPE_VOID) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    }
    lipi_str_appendf(&env->x->text, ".Lendif%d:\n", id);
    return 1;
}

static int gen_begin(CgEnv *env, LipiAst *expr) {
    if (expr->as.items.len == 1) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    }
    for (int i = 1; i < expr->as.items.len; i++) {
        if (!gen_expr(env, (LipiAst *)expr->as.items.items[i])) return 0;
    }
    return 1;
}

static int gen_local_define(CgEnv *env, LipiAst *expr) {
    char *name = typed_name_only(env->c, (LipiAst *)expr->as.items.items[1]);
    CgLocal *local = local_find(env, name);
    if (!local) {
        lipi_diag_error(env->c, ((LipiAst *)expr->as.items.items[1])->span, "backend missing local slot", name);
        return 0;
    }
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[2])) return 0;
    lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", local->offset);
    lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    return 1;
}

static int gen_let(CgEnv *env, LipiAst *expr) {
    LipiAst *bindings = (LipiAst *)expr->as.items.items[1];
    for (int i = 0; i < bindings->as.items.len; i++) {
        LipiAst *binding = (LipiAst *)bindings->as.items.items[i];
        char *name = binding_name_only(env->c, (LipiAst *)binding->as.items.items[0]);
        CgLocal *local = local_find(env, name);
        if (!local) {
            lipi_diag_error(env->c, ((LipiAst *)binding->as.items.items[0])->span,
                            "backend missing let slot", name);
            return 0;
        }
        if (!gen_expr(env, (LipiAst *)binding->as.items.items[1])) return 0;
        lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", local->offset);
    }
    if (expr->as.items.len == 2) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    }
    for (int i = 2; i < expr->as.items.len; i++) {
        if (!gen_expr(env, (LipiAst *)expr->as.items.items[i])) return 0;
    }
    return 1;
}

static int gen_named_let_jump(CgEnv *env, LipiAst *expr, CgNamedLet *target) {
    int argc = expr->as.items.len - 1;
    if (argc > 0) {
        for (int i = 0; i < argc; i++) {
            if (!gen_expr(env, (LipiAst *)expr->as.items.items[i + 1])) return 0;
            push_rax(env);
        }
        for (int i = argc - 1; i >= 0; i--) {
            LipiAst *binding = (LipiAst *)target->bindings->as.items.items[i];
            char *name = binding_name_only(env->c, (LipiAst *)binding->as.items.items[0]);
            CgLocal *local = local_find(env, name);
            if (!local) {
                lipi_diag_error(env->c, ((LipiAst *)binding->as.items.items[0])->span,
                                "backend missing named let slot", name);
                return 0;
            }
            pop_reg(env, "%rax");
            lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", local->offset);
        }
    }
    lipi_str_appendf(&env->x->text, "  jmp .Lnamedlet%d\n", target->label);
    lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    return 1;
}

static int gen_named_let(CgEnv *env, LipiAst *expr) {
    LipiAst *name_node = (LipiAst *)expr->as.items.items[1];
    LipiAst *bindings = (LipiAst *)expr->as.items.items[2];
    for (int i = 0; i < bindings->as.items.len; i++) {
        LipiAst *binding = (LipiAst *)bindings->as.items.items[i];
        char *name = binding_name_only(env->c, (LipiAst *)binding->as.items.items[0]);
        CgLocal *local = local_find(env, name);
        if (!local) {
            lipi_diag_error(env->c, ((LipiAst *)binding->as.items.items[0])->span,
                            "backend missing named let slot", name);
            return 0;
        }
        if (!gen_expr(env, (LipiAst *)binding->as.items.items[1])) return 0;
        lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", local->offset);
    }

    CgNamedLet *info = (CgNamedLet *)lipi_arena_alloc(&env->c->arena, sizeof(CgNamedLet));
    info->name = name_node->as.text;
    info->label = new_label(env->x);
    info->bindings = bindings;
    lipi_vec_push(&env->named_lets, info);
    int named_base = env->named_lets.len - 1;

    lipi_str_appendf(&env->x->text, ".Lnamedlet%d:\n", info->label);
    for (int i = 3; i < expr->as.items.len; i++) {
        if (!gen_expr(env, (LipiAst *)expr->as.items.items[i])) return 0;
    }
    env->named_lets.len = named_base;
    return 1;
}

static int gen_set(CgEnv *env, LipiAst *expr) {
    LipiAst *name_node = (LipiAst *)expr->as.items.items[1];
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[2])) return 0;
    CgLocal *local = local_find(env, name_node->as.text);
    if (local) {
        lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", local->offset);
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    }

    LipiSymbol *sym = find_sym(env->c, name_node->as.text);
    if (!sym || (sym->kind != SYM_GLOBAL && sym->kind != SYM_EXTERN_GLOBAL)) {
        lipi_diag_error(env->c, name_node->span, "backend could not resolve set target", name_node->as.text);
        return 0;
    }
    lipi_str_appendf(&env->x->text, "  mov %%rax, %s(%%rip)\n", symbol_label(env, sym));
    lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    return 1;
}

static int gen_assert(CgEnv *env, LipiAst *expr) {
    int id = new_label(env->x);
    if (!gen_expr(env, (LipiAst *)expr->as.items.items[1])) return 0;
    lipi_str_append(&env->x->text, "  cmp $0, %rax\n");
    lipi_str_appendf(&env->x->text, "  jne .Lassert_ok%d\n", id);
    gen_exit_code(env, 1);
    lipi_str_appendf(&env->x->text, ".Lassert_ok%d:\n  mov $0, %%rax\n", id);
    return 1;
}

static int gen_assert_eq(CgEnv *env, LipiAst *expr) {
    int id = new_label(env->x);
    LipiAst *expected = (LipiAst *)expr->as.items.items[1];
    LipiAst *actual = (LipiAst *)expr->as.items.items[2];
    if (!gen_expr(env, expected)) return 0;
    push_rax(env);
    if (!gen_expr(env, actual)) return 0;
    pop_reg(env, "%rcx");
    if (expected->type == TYPE_STR) {
        lipi_str_append(&env->x->text,
                        "  mov %rcx, %rdi\n"
                        "  mov %rax, %rsi\n"
                        "  call .Llipi_builtin_streq\n"
                        "  cmp $0, %rax\n");
        lipi_str_appendf(&env->x->text, "  jne .Lassert_eq_ok%d\n", id);
    } else {
        lipi_str_append(&env->x->text, "  cmp %rax, %rcx\n");
        lipi_str_appendf(&env->x->text, "  je .Lassert_eq_ok%d\n", id);
    }
    gen_exit_code(env, 1);
    lipi_str_appendf(&env->x->text, ".Lassert_eq_ok%d:\n  mov $0, %%rax\n", id);
    return 1;
}

static int gen_len(CgEnv *env, LipiAst *expr) {
    LipiAst *value = (LipiAst *)expr->as.items.items[1];
    if (!gen_expr(env, value)) return 0;
    if (value->type == TYPE_STR) {
        lipi_str_append(&env->x->text, "  mov %rax, %rdi\n  call .Llipi_builtin_strlen\n");
    } else {
        lipi_str_append(&env->x->text, "  mov (%rax), %rax\n");
    }
    return 1;
}

static int gen_index(CgEnv *env, LipiAst *expr) {
    LipiAst *collection = (LipiAst *)expr->as.items.items[1];
    LipiAst *key = (LipiAst *)expr->as.items.items[2];
    if (!gen_expr(env, collection)) return 0;
    push_rax(env);
    if (!gen_expr(env, key)) return 0;
    pop_reg(env, "%rcx");

    if (collection->type == TYPE_STR) {
        lipi_str_append(&env->x->text, "  movzbq (%rcx,%rax,1), %rax\n");
    } else if (collection->type == TYPE_ARRAY) {
        lipi_str_append(&env->x->text, "  mov 8(%rcx), %rdx\n  mov (%rdx,%rax,8), %rax\n");
    } else if (collection->type == TYPE_MAP) {
        lipi_str_append(&env->x->text, "  mov %rcx, %rdi\n  mov %rax, %rsi\n  call .Llipi_builtin_map_get\n");
    } else {
        lipi_diag_error(env->c, collection->span, "backend cannot index value", NULL);
        return 0;
    }
    return 1;
}

static int gen_append(CgEnv *env, LipiAst *expr) {
    LipiAst *collection = (LipiAst *)expr->as.items.items[1];
    LipiAst *value = (LipiAst *)expr->as.items.items[2];
    if (!gen_expr(env, collection)) return 0;
    push_rax(env);
    if (!gen_expr(env, value)) return 0;
    lipi_str_append(&env->x->text, "  mov %rax, %rsi\n");
    pop_reg(env, "%rdi");
    if (collection->type == TYPE_STR && value->type == TYPE_STR) {
        lipi_str_append(&env->x->text, "  call .Llipi_builtin_string_concat\n");
    } else if (collection->type == TYPE_STR) {
        lipi_str_append(&env->x->text, "  call .Llipi_builtin_string_append_char\n");
    } else if (collection->type == TYPE_ARRAY) {
        lipi_str_append(&env->x->text, "  call .Llipi_builtin_array_append\n");
    } else {
        lipi_diag_error(env->c, collection->span, "backend cannot append to value", NULL);
        return 0;
    }
    return 1;
}

static int gen_for_each(CgEnv *env, LipiAst *expr) {
    LipiAst *name_node = (LipiAst *)expr->as.items.items[1];
    LipiAst *collection = (LipiAst *)expr->as.items.items[2];
    CgLocal *item = local_find(env, name_node->as.text);
    CgLocal *idx = local_find(env, internal_local_name(env->c, expr, "foreach_idx"));
    CgLocal *coll = local_find(env, internal_local_name(env->c, expr, "foreach_collection"));
    if (!item || !idx || !coll) {
        lipi_diag_error(env->c, expr->span, "backend missing for-each slots", NULL);
        return 0;
    }

    int id = new_label(env->x);
    if (!gen_expr(env, collection)) return 0;
    lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", coll->offset);
    lipi_str_appendf(&env->x->text, "  movq $0, %d(%%rbp)\n", idx->offset);
    lipi_str_appendf(&env->x->text, ".Lforeach%d:\n", id);
    if (collection->type == TYPE_STR) {
        lipi_str_appendf(&env->x->text,
                         "  mov %d(%%rbp), %%rdi\n"
                         "  call .Llipi_builtin_strlen\n"
                         "  mov %d(%%rbp), %%rcx\n"
                         "  cmp %%rax, %%rcx\n"
                         "  jge .Lendforeach%d\n"
                         "  mov %d(%%rbp), %%rdx\n"
                         "  movzbq (%%rdx,%%rcx,1), %%rax\n",
                         coll->offset, idx->offset, id, coll->offset);
    } else if (collection->type == TYPE_ARRAY) {
        lipi_str_appendf(&env->x->text,
                         "  mov %d(%%rbp), %%rdx\n"
                         "  mov (%%rdx), %%rax\n"
                         "  mov %d(%%rbp), %%rcx\n"
                         "  cmp %%rax, %%rcx\n"
                         "  jge .Lendforeach%d\n"
                         "  mov 8(%%rdx), %%rdx\n"
                         "  mov (%%rdx,%%rcx,8), %%rax\n",
                         coll->offset, idx->offset, id);
    } else if (collection->type == TYPE_MAP) {
        lipi_str_appendf(&env->x->text,
                         "  mov %d(%%rbp), %%rdx\n"
                         "  mov (%%rdx), %%rax\n"
                         "  mov %d(%%rbp), %%rcx\n"
                         "  cmp %%rax, %%rcx\n"
                         "  jge .Lendforeach%d\n"
                         "  mov 8(%%rdx), %%rdx\n"
                         "  imul $16, %%rcx, %%rcx\n"
                         "  mov 8(%%rdx,%%rcx,1), %%rax\n",
                         coll->offset, idx->offset, id);
    } else {
        lipi_diag_error(env->c, collection->span, "backend cannot iterate value", NULL);
        return 0;
    }
    lipi_str_appendf(&env->x->text, "  mov %%rax, %d(%%rbp)\n", item->offset);
    for (int i = 3; i < expr->as.items.len; i++) {
        if (!gen_expr(env, (LipiAst *)expr->as.items.items[i])) return 0;
    }
    lipi_str_appendf(&env->x->text,
                     "  addq $1, %d(%%rbp)\n"
                     "  jmp .Lforeach%d\n"
                     ".Lendforeach%d:\n"
                     "  mov $0, %%rax\n",
                     idx->offset, id, id);
    return 1;
}

static int gen_inline(CgEnv *env, LipiAst *expr) {
    if (lipi_ast_is_symbol((LipiAst *)expr->as.items.items[1], "asm")) {
        lipi_str_append(&env->x->text, "  # lipi inline asm begin\n");
        lipi_str_append(&env->x->text, ((LipiAst *)expr->as.items.items[2])->as.text);
        if (((LipiAst *)expr->as.items.items[2])->as.text[0] &&
            ((LipiAst *)expr->as.items.items[2])->as.text[strlen(((LipiAst *)expr->as.items.items[2])->as.text) - 1] != '\n') {
            lipi_str_append(&env->x->text, "\n");
        }
        lipi_str_append(&env->x->text, "  # lipi inline asm end\n");
    }
    lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    return 1;
}

static int gen_call(CgEnv *env, LipiAst *expr, const char *name) {
    int argc = expr->as.items.len - 1;
    for (int i = 0; i < argc; i++) {
        if (!gen_expr(env, (LipiAst *)expr->as.items.items[i + 1])) return 0;
        push_rax(env);
    }
    for (int i = argc - 1; i >= 0; i--) {
        pop_reg(env, arg_regs[i]);
    }

    LipiSymbol *sym = find_sym(env->c, name);
    if (!sym) {
        lipi_diag_error(env->c, ((LipiAst *)expr->as.items.items[0])->span, "backend could not resolve call", name);
        return 0;
    }
    int adjust = env->push_depth % 2 != 0;
    if (adjust) {
        lipi_str_append(&env->x->text, "  sub $8, %rsp\n");
    }
    lipi_str_appendf(&env->x->text, "  call %s\n", symbol_label(env, sym));
    if (adjust) {
        lipi_str_append(&env->x->text, "  add $8, %rsp\n");
    }
    if (sym->type == TYPE_VOID) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
    }
    return 1;
}

static int static_qword_supported(LipiAst *value) {
    if (value->kind == AST_INT || value->kind == AST_BOOL || value->kind == AST_STRING) {
        return 1;
    }
    if (value->kind == AST_LIST && value->as.items.len == 2 &&
        ((LipiAst *)value->as.items.items[0])->kind == AST_SYMBOL) {
        LipiTypeKind target = cast_target_from_name(((LipiAst *)value->as.items.items[0])->as.text);
        return target != TYPE_UNKNOWN && static_qword_supported((LipiAst *)value->as.items.items[1]);
    }
    return 0;
}

static int emit_static_qword(CgEnv *env, LipiAst *value) {
    if (value->kind == AST_LIST && value->as.items.len == 2 &&
        ((LipiAst *)value->as.items.items[0])->kind == AST_SYMBOL) {
        return emit_static_qword(env, (LipiAst *)value->as.items.items[1]);
    }
    if (value->kind == AST_INT) {
        lipi_str_appendf(&env->x->data, "  .quad %lld\n", (long long)value->as.int_value);
        return 1;
    }
    if (value->kind == AST_BOOL) {
        lipi_str_appendf(&env->x->data, "  .quad %d\n", value->as.bool_value ? 1 : 0);
        return 1;
    }
    if (value->kind == AST_STRING) {
        const char *label = string_label(env, value->as.text);
        lipi_str_appendf(&env->x->data, "  .quad %s\n", label);
        return 1;
    }
    return 0;
}

static int array_is_static_qword(LipiAst *expr) {
    for (int i = 0; i < expr->as.items.len; i++) {
        if (!static_qword_supported((LipiAst *)expr->as.items.items[i])) return 0;
    }
    return 1;
}

static int gen_array(CgEnv *env, LipiAst *expr) {
    if (!array_is_static_qword(expr)) {
        lipi_diag_error(env->c, expr->span, "unsupported array literal",
                        "x86_64 MVP codegen supports only static qword array/list literals");
        return 0;
    }
    int id = new_label(env->x);
    lipi_str_appendf(&env->x->data, ".Larr_items%d:\n", id);
    if (expr->as.items.len == 0) {
        lipi_str_append(&env->x->data, "  .quad 0\n");
    } else {
        for (int i = 0; i < expr->as.items.len; i++) {
            LipiAst *item = (LipiAst *)expr->as.items.items[i];
            emit_static_qword(env, item);
        }
    }
    lipi_str_appendf(&env->x->data, ".Larr%d:\n  .quad %d\n  .quad .Larr_items%d\n", id, expr->as.items.len, id);
    lipi_str_appendf(&env->x->text, "  lea .Larr%d(%%rip), %%rax\n", id);
    return 1;
}

static int gen_map(CgEnv *env, LipiAst *expr) {
    int id = new_label(env->x);
    int count = expr->as.items.len / 2;
    for (int i = 0; i < count; i++) {
        LipiAst *key = (LipiAst *)expr->as.items.items[i * 2];
        LipiAst *value = (LipiAst *)expr->as.items.items[i * 2 + 1];
        if (!static_qword_supported(value)) {
            lipi_diag_error(env->c, value->span, "unsupported map literal",
                            "x86_64 MVP codegen supports only static qword map values");
            return 0;
        }
        lipi_str_appendf(&env->x->rodata, ".Lmap_key%d_%d:\n  .asciz ", id, i);
        append_asm_string(&env->x->rodata, key->as.text);
        lipi_str_append(&env->x->rodata, "\n");
    }
    lipi_str_appendf(&env->x->data, ".Lmap_entries%d:\n", id);
    for (int i = 0; i < count; i++) {
        LipiAst *value = (LipiAst *)expr->as.items.items[i * 2 + 1];
        lipi_str_appendf(&env->x->data, "  .quad .Lmap_key%d_%d\n", id, i);
        emit_static_qword(env, value);
    }
    lipi_str_appendf(&env->x->data, ".Lmap%d:\n  .quad %d\n  .quad .Lmap_entries%d\n", id, count, id);
    lipi_str_appendf(&env->x->text, "  lea .Lmap%d(%%rip), %%rax\n", id);
    return 1;
}

static int gen_expr(CgEnv *env, LipiAst *expr) {
    switch (expr->kind) {
    case AST_INT:
        lipi_str_appendf(&env->x->text, "  mov $%lld, %%rax\n", (long long)expr->as.int_value);
        return 1;
    case AST_BOOL:
        lipi_str_appendf(&env->x->text, "  mov $%d, %%rax\n", expr->as.bool_value ? 1 : 0);
        return 1;
    case AST_STRING: {
        const char *label = string_label(env, expr->as.text);
        lipi_str_appendf(&env->x->text, "  lea %s(%%rip), %%rax\n", label);
        return 1;
    }
    case AST_SYMBOL:
        return gen_symbol(env, expr);
    case AST_ARRAY:
        return gen_array(env, expr);
    case AST_MAP:
        return gen_map(env, expr);
    case AST_BLOCK:
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    case AST_LIST:
        break;
    }

    if (expr->as.items.len == 0) {
        lipi_str_append(&env->x->text, "  mov $0, %rax\n");
        return 1;
    }
    LipiAst *head = (LipiAst *)expr->as.items.items[0];
    if (head->kind != AST_SYMBOL) {
        lipi_diag_error(env->c, head->span, "backend expected symbolic call head", NULL);
        return 0;
    }
    const char *name = head->as.text;
    if (strcmp(name, "define") == 0) return gen_local_define(env, expr);
    if (strcmp(name, "let") == 0) {
        if (expr->as.items.len >= 4 && ((LipiAst *)expr->as.items.items[1])->kind == AST_SYMBOL) {
            return gen_named_let(env, expr);
        }
        return gen_let(env, expr);
    }
    if (strcmp(name, "set") == 0) return gen_set(env, expr);
    if (strcmp(name, "assert") == 0) return gen_assert(env, expr);
    if (strcmp(name, "assert-eq") == 0) return gen_assert_eq(env, expr);
    if (strcmp(name, "len") == 0) return gen_len(env, expr);
    if (strcmp(name, "index") == 0) return gen_index(env, expr);
    if (strcmp(name, "append") == 0) return gen_append(env, expr);
    if (strcmp(name, "for-each") == 0) return gen_for_each(env, expr);
    if (strcmp(name, "begin") == 0) return gen_begin(env, expr);
    if (strcmp(name, "if") == 0) return gen_if(env, expr);
    if (strcmp(name, "#inline") == 0) return gen_inline(env, expr);
    if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 || strcmp(name, "*") == 0 ||
        strcmp(name, "/") == 0 || strcmp(name, "=") == 0 || strcmp(name, "<") == 0 ||
        strcmp(name, ">") == 0) {
        return gen_binary(env, expr, name);
    }
    LipiTypeKind cast_target = cast_target_from_name(name);
    if (cast_target != TYPE_UNKNOWN) {
        return gen_cast(env, expr, cast_target);
    }
    CgNamedLet *named_let = named_let_find(env, name);
    if (named_let) {
        return gen_named_let_jump(env, expr, named_let);
    }
    return gen_call(env, expr, name);
}

static int emit_function_internal(X86 *x, LipiFunction *fn) {
    LipiCompiler *c = x->c;
    CgEnv env;
    memset(&env, 0, sizeof(env));
    env.x = x;
    env.c = c;
    for (int i = 0; i < fn->params.len; i++) {
        LipiParam *p = (LipiParam *)fn->params.items[i];
        add_local(&env, p->name, p->type);
    }
    for (int i = 0; i < fn->body.len; i++) {
        scan_locals(&env, (LipiAst *)fn->body.items[i]);
    }
    finish_env_layout(&env);

    const char *label = lipi_mangle(&c->arena, fn->name);
    lipi_str_appendf(&x->text, "\n.globl %s\n%s:\n", label, label);
    lipi_str_append(&x->text, "  push %rbp\n  mov %rsp, %rbp\n");
    if (env.stack_size) {
        lipi_str_appendf(&x->text, "  sub $%d, %%rsp\n", env.stack_size);
    }
    for (int i = 0; i < fn->params.len; i++) {
        LipiParam *p = (LipiParam *)fn->params.items[i];
        CgLocal *local = local_find(&env, p->name);
        lipi_str_appendf(&x->text, "  mov %s, %d(%%rbp)\n", arg_regs[i], local->offset);
    }
    for (int i = 0; i < fn->body.len; i++) {
        if (!gen_expr(&env, (LipiAst *)fn->body.items[i])) return 0;
    }
    lipi_str_append(&x->text, "  leave\n  ret\n");
    return 1;
}

static void emit_global_storage(X86 *x) {
    LipiCompiler *c = x->c;
    for (int i = 0; i < c->globals.len; i++) {
        LipiGlobal *g = (LipiGlobal *)c->globals.items[i];
        const char *label = lipi_mangle(&c->arena, g->name);
        lipi_str_appendf(&x->data, ".globl %s\n%s:\n  .quad 0\n", label, label);
    }
}

static void emit_global_inline_asm(X86 *x) {
    LipiCompiler *c = x->c;
    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (!is_inline_mode(form, "asm-global")) {
            continue;
        }
        const char *text = ((LipiAst *)form->as.items.items[2])->as.text;
        lipi_str_append(&x->text, "\n# lipi global inline asm begin\n");
        lipi_str_append(&x->text, text);
        if (text[0] && text[strlen(text) - 1] != '\n') {
            lipi_str_append(&x->text, "\n");
        }
        lipi_str_append(&x->text, "# lipi global inline asm end\n");
    }
}

static int emit_top_level(X86 *x) {
    LipiCompiler *c = x->c;
    CgEnv env;
    memset(&env, 0, sizeof(env));
    env.x = x;
    env.c = c;
    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (is_head(form, "define")) {
            LipiAst *target = (LipiAst *)form->as.items.items[1];
            if (target->kind == AST_LIST) continue;
            continue;
        }
        if (is_head(form, "#extern")) continue;
        if (is_inline_mode(form, "asm-global")) continue;
        scan_locals(&env, form);
    }
    finish_env_layout(&env);

    lipi_str_append(&x->text, "\n.globl _start\n_start:\n  mov %rsp, %rbp\n");
    if (env.stack_size) {
        lipi_str_appendf(&x->text, "  sub $%d, %%rsp\n", env.stack_size);
    }

    LipiTypeKind final_type = TYPE_VOID;
    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (is_head(form, "define")) {
            LipiAst *target = (LipiAst *)form->as.items.items[1];
            if (target->kind == AST_LIST) {
                continue;
            }
            char *name = typed_name_only(c, target);
            LipiSymbol *sym = find_sym(c, name);
            if (!sym || !gen_expr(&env, (LipiAst *)form->as.items.items[2])) return 0;
            lipi_str_appendf(&x->text, "  mov %%rax, %s(%%rip)\n", symbol_label(&env, sym));
            final_type = TYPE_VOID;
            continue;
        }
        if (is_head(form, "#extern")) {
            final_type = TYPE_VOID;
            continue;
        }
        if (is_inline_mode(form, "asm-global")) {
            final_type = TYPE_VOID;
            continue;
        }
        if (!gen_expr(&env, form)) return 0;
        final_type = form->type;
    }

    if (lipi_type_is_integer(final_type) || final_type == TYPE_BOOL) {
        lipi_str_append(&x->text, "  mov %rax, %rdi\n");
    } else {
        lipi_str_append(&x->text, "  mov $0, %rdi\n");
    }
    lipi_str_append(&x->text, "  mov $60, %rax\n  syscall\n");
    return 1;
}

static int emit_test_start(X86 *x) {
    LipiCompiler *c = x->c;
    CgEnv env;
    memset(&env, 0, sizeof(env));
    env.x = x;
    env.c = c;

    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (is_head(form, "define")) {
            LipiAst *target = (LipiAst *)form->as.items.items[1];
            if (target->kind != AST_LIST) {
                scan_locals(&env, (LipiAst *)form->as.items.items[2]);
            }
        }
    }
    finish_env_layout(&env);

    lipi_str_append(&x->text, "\n.globl _start\n_start:\n  mov %rsp, %rbp\n");
    if (env.stack_size) {
        lipi_str_appendf(&x->text, "  sub $%d, %%rsp\n", env.stack_size);
    }

    for (int i = 0; i < c->forms.len; i++) {
        LipiAst *form = (LipiAst *)c->forms.items[i];
        if (is_head(form, "define")) {
            LipiAst *target = (LipiAst *)form->as.items.items[1];
            if (target->kind == AST_LIST) {
                continue;
            }
            char *name = typed_name_only(c, target);
            LipiSymbol *sym = find_sym(c, name);
            if (!sym || !gen_expr(&env, (LipiAst *)form->as.items.items[2])) return 0;
            lipi_str_appendf(&x->text, "  mov %%rax, %s(%%rip)\n", symbol_label(&env, sym));
        }
    }

    for (int i = 0; i < c->functions.len; i++) {
        LipiFunction *fn = (LipiFunction *)c->functions.items[i];
        if (!is_entry_test_function(c, fn)) {
            continue;
        }
        int id = new_label(x);
        lipi_str_appendf(&x->text, "  call %s\n", lipi_mangle(&c->arena, fn->name));
        if (fn->ret_type == TYPE_BOOL) {
            lipi_str_append(&x->text, "  cmp $0, %rax\n");
            lipi_str_appendf(&x->text, "  jne .Ltest_ok%d\n", id);
            gen_exit_code(&env, 1);
            lipi_str_appendf(&x->text, ".Ltest_ok%d:\n", id);
        } else if (lipi_type_is_integer(fn->ret_type)) {
            lipi_str_append(&x->text, "  cmp $0, %rax\n");
            lipi_str_appendf(&x->text, "  je .Ltest_ok%d\n", id);
            gen_exit_code(&env, 1);
            lipi_str_appendf(&x->text, ".Ltest_ok%d:\n", id);
        }
    }

    gen_exit_code(&env, 0);
    return 1;
}

static int x86_emit_program(LipiCompiler *compiler) {
    X86 x;
    memset(&x, 0, sizeof(x));
    x.c = compiler;
    lipi_str_append(&x.text, ".text\n");
    lipi_str_append(&x.rodata, ".section .rodata\n");
    lipi_str_append(&x.data, ".data\n.align 8\n");
    emit_builtin_helpers(&x);
    emit_global_storage(&x);
    emit_global_inline_asm(&x);

    for (int i = 0; i < compiler->functions.len; i++) {
        if (!emit_function_internal(&x, (LipiFunction *)compiler->functions.items[i])) {
            return 0;
        }
    }
    if (compiler->test_mode ? !emit_test_start(&x) : !emit_top_level(&x)) {
        return 0;
    }

    FILE *f = fopen(compiler->asm_path, "wb");
    if (!f) {
        perror(compiler->asm_path);
        return 0;
    }
    fwrite(x.text.data, 1, x.text.len, f);
    if (x.rodata.len > strlen(".section .rodata\n")) {
        fwrite(x.rodata.data, 1, x.rodata.len, f);
    }
    fwrite(x.data.data, 1, x.data.len, f);
    fclose(f);
    lipi_str_free(&x.text);
    lipi_str_free(&x.rodata);
    lipi_str_free(&x.data);
    return 1;
}

static int x86_emit_function_api(LipiCompiler *compiler, LipiFunction *fn) {
    (void)compiler;
    (void)fn;
    return 0;
}

static int x86_emit_expr_api(LipiCompiler *compiler, LipiAst *expr) {
    (void)compiler;
    (void)expr;
    return 0;
}

const LipiBackend *lipi_backend_x86_64(void) {
    static LipiBackend backend = {
        "x86_64-linux",
        x86_emit_program,
        x86_emit_function_api,
        x86_emit_expr_api
    };
    return &backend;
}

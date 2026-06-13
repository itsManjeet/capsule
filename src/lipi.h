#ifndef LIPI_H
#define LIPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LipiCompiler LipiCompiler;
typedef struct LipiAst LipiAst;
typedef struct LipiFunction LipiFunction;
typedef struct LipiBackend LipiBackend;

typedef struct {
    const char *file;
    int line;
    int column;
    int length;
} LipiSpan;

typedef struct {
    void **items;
    int len;
    int cap;
} LipiVec;

typedef struct LipiArenaChunk {
    struct LipiArenaChunk *next;
    size_t cap;
    size_t used;
    unsigned char data[];
} LipiArenaChunk;

typedef struct {
    LipiArenaChunk *chunks;
} LipiArena;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} LipiStr;

typedef enum {
    LIPI_OK = 0,
    LIPI_ERR = 1
} LipiStatus;

typedef enum {
    TOK_EOF,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_QUOTE,
    TOK_BACKQUOTE,
    TOK_COMMA,
    TOK_COMMA_AT,
    TOK_ELLIPSIS,
    TOK_COLON,
    TOK_INT,
    TOK_BOOL,
    TOK_STRING,
    TOK_SYMBOL,
    TOK_BLOCK
} LipiTokenKind;

typedef struct {
    LipiTokenKind kind;
    LipiSpan span;
    char *text;
    int64_t int_value;
    int bool_value;
} LipiToken;

typedef enum {
    AST_INT,
    AST_BOOL,
    AST_STRING,
    AST_SYMBOL,
    AST_LIST,
    AST_ARRAY,
    AST_MAP,
    AST_BLOCK
} LipiAstKind;

typedef enum {
    TYPE_UNKNOWN,
    TYPE_I8,
    TYPE_I16,
    TYPE_I32,
    TYPE_I64,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_PTR,
    TYPE_STR,
    TYPE_ARRAY,
    TYPE_MAP
} LipiTypeKind;

struct LipiAst {
    LipiAstKind kind;
    LipiSpan span;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    union {
        int64_t int_value;
        int bool_value;
        char *text;
        LipiVec items;
    } as;
};

typedef struct {
    char *name;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    LipiSpan span;
} LipiParam;

typedef enum {
    SYM_GLOBAL,
    SYM_FUNCTION,
    SYM_EXTERN_GLOBAL,
    SYM_EXTERN_FUNCTION
} LipiSymbolKind;

typedef struct {
    char *name;
    LipiSymbolKind kind;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    LipiVec params; /* LipiParam* */
    LipiAst *node;
    LipiSpan span;
} LipiSymbol;

typedef struct {
    char *name;
    LipiTypeKind type;
    LipiTypeKind elem_type;
    LipiAst *init;
    LipiSpan span;
} LipiGlobal;

struct LipiFunction {
    char *name;
    LipiTypeKind ret_type;
    LipiVec params; /* LipiParam* */
    LipiVec body;   /* LipiAst* */
    LipiAst *node;
    LipiSpan span;
};

typedef struct {
    char *name;
    LipiVec params; /* char* */
    int variadic;
    char *variadic_name;
    LipiVec body; /* LipiAst* */
    LipiSpan span;
} LipiMacro;

typedef struct {
    char *path;
    char *text;
    size_t len;
    int *line_offsets;
    int line_count;
} LipiSource;

typedef struct {
    LipiSpan primary;
    LipiSpan secondary;
    int has_secondary;
    char message[512];
    char detail[512];
    char secondary_message[256];
} LipiDiag;

struct LipiBackend {
    const char *name;
    int (*emit_program)(LipiCompiler *compiler);
    int (*emit_function)(LipiCompiler *compiler, LipiFunction *fn);
    int (*emit_expr)(LipiCompiler *compiler, LipiAst *expr);
};

struct LipiCompiler {
    LipiArena arena;
    LipiVec sources;       /* LipiSource* */
    LipiVec forms;         /* LipiAst* */
    LipiVec symbols;       /* LipiSymbol* */
    LipiVec globals;       /* LipiGlobal* */
    LipiVec functions;     /* LipiFunction* */
    LipiVec macros;        /* LipiMacro* */
    LipiVec inline_c;      /* char* */
    LipiVec loaded_files;  /* ModuleState* */
    LipiVec diagnostics;   /* LipiDiag* */
    const LipiBackend *backend;
    const char *target;
    const char *arch;
    const char *platform;
    const char *stdlib_path;
    const char *entry_file;
    const char *test_filter;
    const char *asm_path;
    int test_mode;
    int had_error;
    int quiet;
};

typedef struct {
    LipiSource *source;
    const char *cur;
    int pos;
    int line;
    int col;
    LipiToken token;
    LipiCompiler *compiler;
} LipiLexer;

typedef struct {
    LipiLexer lexer;
    LipiCompiler *compiler;
} LipiParser;

/* util.c */
void lipi_vec_push(LipiVec *v, void *item);
void lipi_vec_insert_vec(LipiVec *dst, LipiVec *src);
void *lipi_arena_alloc(LipiArena *arena, size_t size);
char *lipi_arena_strdup(LipiArena *arena, const char *s);
char *lipi_arena_strndup(LipiArena *arena, const char *s, size_t n);
void lipi_arena_free(LipiArena *arena);
void lipi_str_append(LipiStr *s, const char *text);
void lipi_str_append_n(LipiStr *s, const char *text, size_t n);
void lipi_str_appendf(LipiStr *s, const char *fmt, ...);
void lipi_str_free(LipiStr *s);
int lipi_ends_with(const char *s, const char *suffix);
int lipi_contains_char(const char *s, char c);
char *lipi_path_dirname(LipiArena *arena, const char *path);
char *lipi_path_join(LipiArena *arena, const char *a, const char *b);
char *lipi_mangle(LipiArena *arena, const char *name);
int lipi_run(char *const argv[]);

/* source.c / diag.c */
LipiSource *lipi_source_load(LipiCompiler *c, const char *path);
LipiSource *lipi_source_from_text(LipiCompiler *c, const char *path, const char *text);
LipiSource *lipi_source_find(LipiCompiler *c, const char *path);
void lipi_diag_error(LipiCompiler *c, LipiSpan span, const char *message, const char *detail);
void lipi_diag_error2(LipiCompiler *c, LipiSpan span, const char *message, const char *detail,
                      LipiSpan secondary, const char *secondary_message);

/* lexer.c */
void lipi_lexer_init(LipiLexer *lx, LipiCompiler *c, LipiSource *source);
int lipi_lexer_next(LipiLexer *lx);

/* parser.c */
int lipi_parse_source(LipiCompiler *c, LipiSource *source, LipiVec *out_forms);

/* ast.c */
LipiAst *lipi_ast_new(LipiCompiler *c, LipiAstKind kind, LipiSpan span);
LipiAst *lipi_ast_symbol(LipiCompiler *c, const char *name, LipiSpan span);
LipiAst *lipi_ast_list2(LipiCompiler *c, const char *head, LipiAst *arg, LipiSpan span);
LipiAst *lipi_ast_clone(LipiCompiler *c, LipiAst *node);
int lipi_ast_is_symbol(LipiAst *node, const char *name);
char *lipi_ast_symbol_name(LipiAst *node);
LipiAst *lipi_list_get(LipiAst *list, int index);

/* module.c */
int lipi_load_program(LipiCompiler *c, const char *path);

/* macro.c */
int lipi_expand_macros(LipiCompiler *c);

/* types.c */
const char *lipi_type_name(LipiTypeKind type);
LipiTypeKind lipi_type_from_name(const char *name);
int lipi_type_is_integer(LipiTypeKind type);
int lipi_type_is_signed_integer(LipiTypeKind type);
int lipi_type_is_unsigned_integer(LipiTypeKind type);
int lipi_type_is_pointer_like(LipiTypeKind type);
int lipi_type_is_runtime_scalar(LipiTypeKind type);
int lipi_type_int_bits(LipiTypeKind type);
int lipi_split_typed_name(LipiCompiler *c, const char *text, char **name, char **type_name);

/* sema.c */
int lipi_sema_check(LipiCompiler *c);
LipiSymbol *lipi_symbol_find(LipiCompiler *c, const char *name);

/* codegen.c / backend_x86_64.c */
int lipi_emit_assembly(LipiCompiler *c, const char *path, const char *target);
const LipiBackend *lipi_backend_x86_64(void);

/* inline_c.c */
int lipi_collect_inline_c(LipiCompiler *c);
int lipi_compile_inline_c(LipiCompiler *c, const char *tmpdir, char *obj_path, size_t obj_path_cap);

/* fmt.c */
int lipi_format_file(LipiCompiler *c, const char *path);
int lipi_format_source_text(LipiCompiler *c, const char *path, const char *text, LipiStr *out);

/* lsp.c */
int lipi_lsp_run(LipiCompiler *base);

#endif

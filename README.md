# LIPI

LIPI is a tiny compiled Lisp/Scheme-like language implemented in pure C. It reads `.lipi` files with S-expression syntax, expands imports and macros, performs symbol and type checks, emits GNU assembler for `x86_64-linux`, assembles with `as`, and links with `ld` into a native Linux executable.

There is no user-written `main`. A source file is the program entry point; `lipi` emits `_start` and exits with the final integer expression value, or `0` for `none`/no useful final value.

## Build

```sh
make
```

This creates:

```sh
./lipi
```

Run the project test suites:

```sh
make test
```

`make test` runs compiler/internal fixtures first from `tests/compiler/`, then standard library tests from the library files themselves with `lipi -test`. The runners print each case plus passed/failed totals and failure details.

## Commands

```sh
./lipi  examples/hello.lipi
echo $?

./lipi -build hello examples/hello.lipi
./lipi -asm add.s examples/add.lipi
./lipi -check examples/macro.lipi
./lipi -fmt examples/macro.lipi
./lipi -lsp
./lipi -test example_test.lipi
./lipi -test example_test.lipi test-add
./lipi -arch x86_64 -platform linux examples/hello.lipi
```

By default, `lipi` compiles a `.lipi` file to a temporary native executable, runs it, removes the executable, and returns the program's exit status.

`-build` emits assembly, assembles with `as`, optionally compiles inline C blocks with `$CC` or `cc`, then links with `ld` and leaves the executable at the requested path. `-asm` writes generated assembly and stops. `-check` only runs the frontend checks. `-fmt` formats the source file in place while preserving comments, reader quote shorthand, and inline triple-backtick blocks. `-lsp` starts a stdio Language Server Protocol server for editor integration. `-arch` and `-platform` select the runtime profile and backend target; the default is `x86_64` and `linux`.

## Language Server

Start the language server with:

```sh
./lipi -lsp
```

The server speaks standard LSP over stdin/stdout. Current capabilities:

- `initialize`, `shutdown`, and `exit`
- full-document `textDocument/didOpen`, `didChange`, `didSave`, and `didClose`
- `textDocument/publishDiagnostics` using the compiler frontend diagnostics
- `textDocument/formatting` using the same formatter as `-fmt`

The server keeps open documents in memory, so diagnostics and formatting work on unsaved editor buffers. Imports still resolve using normal LIPI import rules, with open in-memory documents preferred when their paths match imported files.

## Zed Extension

The Zed editor extension lives in `ext/zed/`.

```sh
make
cargo check --manifest-path ext/zed/Cargo.toml
```

Install it in Zed with `zed: install dev extension`, then select `ext/zed`.

The extension associates `.lipi` files with the LIPI language, provides tree-sitter highlighting through `tree-sitter-scheme`, and starts `lipi -lsp` for diagnostics and formatting. It resolves the compiler from `LIPI_LIPI  `, then from `lipi` at the workspace root, then from `PATH`.

## Built-In Test Runner

`lipi -test` builds a temporary native test executable with an internal test entry point, runs it, and returns the test executable's status.

Test functions are collected from the entry file only, not imported modules. A test function:

- has a name starting with `test-`
- takes no arguments
- returns `none`, `bool`, or an integer type

`none` tests pass unless an assertion fails. `bool` tests pass when they return true. Integer tests pass when they return `0`. `void` is still accepted as a compatibility alias for `none`.

```lisp
(define (test-add:none)
  (assert (= (+ 20 22) 42))
  (assert-eq 42 (+ 20 22)))
```

Run all tests in the file:

```sh
./lipi -test example_test.lipi
```

Run one test:

```sh
./lipi -test example_test.lipi test-add
```

Built-in test assertions:

```lisp
(assert condition)
(assert-eq expected actual)
```

## Imports

Imports behave like AST-level source inclusion:

```lisp
(#import "math")
```

Module names map to `.lipi` files. Search order is:

1. The importing file's directory
2. `LIPI_MODULE_PATH` directories, separated by `:`
3. The bundled standard library directory, or `LIPI_STDLIB_PATH` when set
4. `/usr/lib/lipi`

Paths containing `/` or ending in `.lipi` are treated as direct paths relative to the importing file.

Import strings can use runtime profile placeholders:

```lisp
(#import "runtime-<arch>-<platform>")
```

The compiler expands those from `-arch` and `-platform`.

## Types

The scalar types are:

```text
i8 i16 i32 i64
u8 u16 u32 u64
bool
ptr
none
str
array
map
```

Integer literals default to `i64`, but fitting literals may initialize narrower integer declarations:

```lisp
(define byte:u8 42)
(define signed:i16 -10)
```

Arithmetic and integer comparisons require matching integer types. Use explicit conversions when crossing widths or signedness:

```lisp
(+ (u8 40) (u8 2))
(u8->i64 (u8 42))
(bool->u32 true)
(i64->bool 1)
```

The type name itself is also a cast form, such as `(i32 value)`, `(u64 value)`, or `(ptr value)`. Arrow casts validate the source side, so `(i32->u8 value)` requires `value` to type-check as `i32`.

Pointer-like values can be converted through `ptr` for heap/runtime interop:

```lisp
(str->ptr "hello")
(ptr->str some-ptr)
(array->ptr xs)
(ptr->map p)
```

## Local Bindings

`let` creates local stack bindings with inferred types:

```lisp
(let ((x 40)
      (y 2))
  (+ x y))
```

Typed bindings are also accepted:

```lisp
(let ((x:i64 1))
  (set x 40)
  (+ x 2))
```

Binding initializers are checked before the new names are added to the local scope. Bodies may contain multiple expressions; the final expression is the result.

Named `let` creates a local jump target over its bindings:

```lisp
(let loop ((i 0)
           (sum 0))
  (if (< i 6)
      (loop (+ i 1) (+ sum 7))
      sum))
```

Inside the body, `(loop)` jumps back using current binding values. `(loop value...)` evaluates all values, updates the bindings in order, then jumps back.

`set` assigns an existing local or global variable and returns `none`:

```lisp
(define answer:i64 0)
(set answer 42)
answer
```

## Collections

Array literals are the current list representation:

```lisp
[1 2 3]
```

`list` is accepted as a type annotation alias for `array`.

Maps are string-keyed qword-value objects:

```lisp
{"name" "LIPI" "version" 1}
```

Array elements must all have the same type. Map values must all have the same type. Static collection values currently support qword-compatible values such as integers, `bool`, and `str`.

The built-in helpers are type checked:

```lisp
(len "hello")                 ; str -> i64
(len [1 2 3])                 ; list/array -> i64
(len {"a" 1 "b" 2})           ; map -> i64

(index "abc" 0)               ; string byte as i64
(index [10 20 30] 1)          ; list/array element
(index {"answer" 42} "answer") ; map value

(append "hi" "!")             ; new string
(append "hi" 33)              ; append byte value, returns new string
(append [1 2] 3)              ; new list/array
```

`append` returns a new collection; use `set` to keep it:

```lisp
(let ((xs [1 2]))
  (set xs (append xs 3))
  (index xs 2))
```

`for-each` iterates strings, lists/arrays, and maps. For strings the loop value is the byte as `i64`; for maps the loop value is each map value.

```lisp
(let ((sum 0))
  (for-each x [10 20 12]
    (set sum (+ sum x)))
  sum)
```

C-compatible runtime shapes are available for inline C via `#include "lipi_runtime.h"`:

```c
typedef int8_t lipi_i8;
typedef int16_t lipi_i16;
typedef int32_t lipi_i32;
typedef long lipi_i64;
typedef uint8_t lipi_u8;
typedef uint16_t lipi_u16;
typedef uint32_t lipi_u32;
typedef unsigned long lipi_u64;
typedef long lipi_bool;
typedef void *lipi_ptr;
typedef const char *lipi_str;

struct lipi_array {
    long len;
    long *items;
};

typedef struct lipi_array lipi_list;

struct lipi_map_entry {
    const char *key;
    long value;
};

struct lipi_map {
    long len;
    struct lipi_map_entry *entries;
};
```

The header also exposes offset macros such as `LIPI_ARRAY_LEN_OFFSET`, `LIPI_ARRAY_ITEMS_OFFSET`, `LIPI_MAP_LEN_OFFSET`, `LIPI_MAP_ENTRIES_OFFSET`, `LIPI_MAP_ENTRY_KEY_OFFSET`, and `LIPI_MAP_ENTRY_VALUE_OFFSET`.

`lipi` passes the standard library directory as an include path when compiling inline C.

## Macros

Macros are compile-time only:

```lisp
(define-macro (inc x)
  `(+ ,x 1))

(inc 41)
```

The MVP supports quote/quasiquote, unquote, unquote-splicing, variadic macro parameters such as `body...`, and small macro-time helpers including `if`, `=`, `length`, `list`, `cons`, `car`, `cdr`, `append`, predicates, `macro-error`, and `gensym`.

## Inline Assembly

Inline assembly is passed through to generated assembly:

```lisp
(#inline asm ```
  mov $60, %rax
  mov $42, %rdi
  syscall
```)
```

For `x86_64-linux`, inline assembly uses GNU assembler AT&T syntax.

Runtime profiles can use `asm-global` to emit callable assembly helpers into `.text` without executing them from `_start`:

```lisp
(#inline asm-global ```
.globl some_runtime_helper
some_runtime_helper:
  ret
```)
```

Normal `#inline asm` remains executable inline program/function code.

## Standard Library

The bundled standard library lives in `lib/`:

- `core.lipi` provides runtime-independent macros/functions such as `do`, `when`, `unless`, `cond`, `not`, and identity helpers.
- `std.lipi` imports `core` and is the public standard prelude module.
- `syscall.lipi` provides x86_64-linux syscall constants, low-level syscall helpers, and runtime allocation shims.
- `runtime.lipi` wraps syscall/platform helpers as `runtime-read`, `runtime-write`, `runtime-write-str`, `runtime-exit`, `runtime-alloc`, and `runtime-free`.
- `alloc.lipi` provides a simple brk-backed free-list allocator with `malloc` and `free`.
- `math.lipi` provides small i64 math helpers such as `math-abs`, `math-min`, `math-max`, `math-clamp`, `math-square`, and `math-pow`.
- `os.lipi` provides OS helpers layered on the runtime profile, including `os-exit`, `os-read`, `os-write`, `os-write-str`, `os-alloc`, and `os-free`.

Example:

```lisp
(#import "std")

(when true
  42)
```

`std.lipi` also provides a standard `cond` macro:

```lisp
(cond
  ((= value 1) 10)
  ((= value 2)
    (define answer:i64 40)
    (+ answer 2))
  (else 0))
```

Each clause is `(condition body...)`; `else` is the fallback clause. Bodies may contain multiple expressions and are expanded as `(begin body...)`.

The generic syscall macro currently supports 0 to 5 syscall arguments. Syscalls that need pointer-typed arguments should use typed runtime/OS helpers until the compiler grows richer ABI support and typed casts.

## Inline C

Inline C blocks are extracted into a temporary C file only when present:

```lisp
(#inline c ```
long c_add(long a, long b) {
    return a + b;
}
```)

(#extern (c_add:i64 a:i64 b:i64))
(c_add 20 22)
```

The generated C is compiled with `$CC` if set, otherwise `cc`, and linked with the generated LIPI object.

## Externs

Extern declarations make symbols visible to semantic analysis and codegen:

```lisp
(#extern errno:i64)
(#extern (exit:void code:i64))
```

Extern functions are emitted as external references. The current direct `ld` flow is intentionally minimal, so libc integration is not yet automated.

## Current Limitations

- Target support is currently `x86_64-linux` only.
- Calls support the first six integer/pointer ABI arguments.
- Narrow integer values are type-checked and truncated/sign-extended at cast/arithmetic boundaries, but stack/global slots and ABI passing are currently qword-backed.
- Collections use qword storage for elements and map values; element/value types are tracked by the checker.
- `append` currently allocates with a simple mmap helper and does not free old collection storage.
- Macro evaluation is intentionally small and focused on syntax-template macros.
- There is a minimal mmap/munmap-backed runtime allocator, but no garbage collector.
- The generic syscall macro supports up to 5 syscall arguments for now; syscall6 needs stack-argument ABI support.
- Optimization is deliberately absent; emitted assembly favors readability.

## Backend Plan

The parser, AST, module loader, macro expander, semantic analyzer, and type checker are backend-independent. The backend boundary is `LipiBackend`, currently implemented by `backend_x86_64.c`. A future `aarch64-linux` backend can reuse the front end and add target-specific ABI lowering, assembly emission, syscall exit, and object/link behavior.

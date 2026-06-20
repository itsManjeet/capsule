# srclang

`srclang` is a pure C, native assembly-oriented language compiler.

Pipeline:

```text
source -> scanner -> tokens -> parser -> AST -> typed bytecode IR -> x86-64/ARM64 assembly
```

The parser follows `grammar.txt`, including compile-time imports and type syntax. The current native backend lowers functions, top-level variables, integer/pointer/raw-string-sized values, direct calls, returns, `if`, `while`, and integer arithmetic/comparisons to real x86-64 or ARM64 assembly. The active pipeline emits native assembly and then uses the configured assembler/linker to produce native binaries, objects, dynamic libraries, or archives.

## Primitive Types

Builtin primitive types are lowercase only:

```srclang
bool nil none
u8 u16 u32 u64
i8 i16 i32 i64
f32 f64
str string ptr
num any
```

`str` is a raw immutable string pointer. `string` is reserved for the future internal string class type. Integer-family values currently lower as 64-bit machine values in the native backend.

## Extern Functions

Extern declarations bind calls to linker symbols directly:

```srclang
extern fun printf(format: ptr, ...) i32;

printf("hello from srclang\n");
0;
```

Variadic extern calls are supported for register arguments currently handled by the native backend.

## Inline Assembly

Inline assembly is available as a statement-only compiler builtin:

```srclang
asm("
  mov rax, rax
");
```

The string is injected into the current generated function for the selected target. Ordinary string literals are multiline by default, so no separate triple-quote syntax is needed. Inline assembly is raw target assembly and must match the selected backend.

## Imports

Imports are compile-time only:

```srclang
import support.math;
```

`import support.math;` resolves to `support/math.src`. The resolver searches the importing file's directory, paths in `SRCLANG_PATH`, and the current directory. Imported modules are parsed before bytecode generation, and only referenced top-level `let`, `fun`, and `class` declarations plus their transitive top-level dependencies are copied into the final program IR.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

Compile and run:

```sh
build/srclang tests/native_main.src
```

Build a binary:

```sh
build/srclang tests/native_main.src -bin build/native_main
```

Emit one assembly file per input:

```sh
build/srclang tests/native_main.src -S build/asm
build/srclang tests/native_main.src -target arm64 -S build/arm64-asm
```

Emit one object file per input:

```sh
build/srclang tests/native_main.src -object build/obj
```

Build libraries:

```sh
build/srclang tests/library.src -lib build/libexample.so
build/srclang tests/library.src -archive build/libexample.a
```

Tool selection:

```sh
build/srclang tests/native_main.src -bin build/native_main -target native -as cc -asflags "-g" -ld cc -ldflags "-lm"
```

Supported targets are `native`, `x64`, `x86_64`, `arm64`, and `aarch64`. `srclang <files...>` only runs native-target output; use `-bin`, `-object`, `-lib`, `-archive`, or `-S` for cross-target output with the matching assembler/linker.

# LIPI for Zed

Zed language support for LIPI.

## Features

- `.lipi` file association
- S-expression syntax highlighting through `tree-sitter-scheme`
- outline entries for `define` and `define-macro`
- diagnostics from `lipi -lsp`
- document formatting from `lipi -lsp`

## Local Install

Build the compiler first from the repository root:

```sh
make
rustup target add wasm32-wasip2
cargo check --manifest-path ext/zed/Cargo.toml
```

Install this directory as a Zed dev extension:

```text
zed: install dev extension
```

Then select:

```text
ext/zed
```

## Language Server Resolution

The extension starts:

```sh
lipi -lsp
```

Resolution order:

1. `LIPI_LIPI` from the shell environment
2. `lipi` at the active worktree root
3. `lipi` on `PATH`

When the active worktree contains `lib/std.lipi`, the extension sets `LIPI_STDLIB_PATH` to that worktree's `lib` directory unless the environment already provides it.

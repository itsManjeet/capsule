#!/bin/sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/lipi-compiler-tests-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

TOTAL=0
PASSED=0
FAILED=0
REPORT="$TMP/report"
: > "$REPORT"

record_pass() {
  TOTAL=$((TOTAL + 1))
  PASSED=$((PASSED + 1))
  printf 'ok %d - %s\n' "$TOTAL" "$1"
}

record_fail() {
  TOTAL=$((TOTAL + 1))
  FAILED=$((FAILED + 1))
  printf 'not ok %d - %s\n' "$TOTAL" "$1"
  {
    printf '\n[%s]\n' "$1"
    cat "$2"
  } >> "$REPORT"
}

expect_exit_42() {
  src=$1
  name="run ${src#$ROOT/}"
  out="$TMP/out"
  err="$TMP/err"
  "$ROOT/lipi" "$src" >"$out" 2>"$err"
  code=$?
  if [ "$code" -eq 42 ]; then
    record_pass "$name"
  else
    {
      printf 'exit code: %d, expected 42\n' "$code"
      printf '\nstdout:\n'
      cat "$out"
      printf '\nstderr:\n'
      cat "$err"
    } > "$TMP/detail"
    record_fail "$name" "$TMP/detail"
  fi
}

expect_error() {
  src=$1
  pattern_file=$2
  pattern=$(cat "$pattern_file")
  name="error ${src#$ROOT/}"
  out="$TMP/out"
  err="$TMP/err"
  if "$ROOT/lipi" -check "$src" >"$out" 2>"$err"; then
    {
      printf 'expected diagnostic containing: %s\n' "$pattern"
      printf '\nstdout:\n'
      cat "$out"
      printf '\nstderr:\n'
      cat "$err"
    } > "$TMP/detail"
    record_fail "$name" "$TMP/detail"
    return
  fi
  if grep -q "$pattern" "$err"; then
    record_pass "$name"
  else
    {
      printf 'missing diagnostic containing: %s\n' "$pattern"
      printf '\nstdout:\n'
      cat "$out"
      printf '\nstderr:\n'
      cat "$err"
    } > "$TMP/detail"
    record_fail "$name" "$TMP/detail"
  fi
}

expect_ok_command() {
  name=$1
  shift
  out="$TMP/out"
  err="$TMP/err"
  if "$@" >"$out" 2>"$err"; then
    record_pass "$name"
  else
    {
      printf 'command failed:'
      printf ' %s' "$@"
      printf '\n\nstdout:\n'
      cat "$out"
      printf '\nstderr:\n'
      cat "$err"
    } > "$TMP/detail"
    record_fail "$name" "$TMP/detail"
  fi
}

expect_grep() {
  name=$1
  pattern=$2
  file=$3
  if grep -q "$pattern" "$file"; then
    record_pass "$name"
  else
    {
      printf 'missing pattern: %s\n' "$pattern"
      printf 'file: %s\n\n' "$file"
      cat "$file"
    } > "$TMP/detail"
    record_fail "$name" "$TMP/detail"
  fi
}

for src in "$ROOT"/tests/pass/*.lipi; do
  expect_exit_42 "$src"
done

expect_ok_command "-build function_call" \
  "$ROOT/lipi" -build "$TMP/function_call" "$ROOT/tests/pass/function_call.lipi"
"$TMP/function_call" >"$TMP/build-run.out" 2>"$TMP/build-run.err"
code=$?
if [ "$code" -eq 42 ]; then
  record_pass "-build executable exits 42"
else
  {
    printf 'exit code: %d, expected 42\n' "$code"
    cat "$TMP/build-run.out" "$TMP/build-run.err"
  } > "$TMP/detail"
  record_fail "-build executable exits 42" "$TMP/detail"
fi

expect_ok_command "-asm function_call" \
  "$ROOT/lipi" -asm "$TMP/function_call.s" "$ROOT/tests/pass/function_call.lipi"
expect_grep "-asm output contains _start" "_start" "$TMP/function_call.s"

expect_ok_command "-check macro_inc" \
  "$ROOT/lipi" -check "$ROOT/tests/pass/macro_inc.lipi"

FMT_SRC="$TMP/fmt_source.lipi"
cat > "$FMT_SRC" <<'EOF'
;fmt comment
(define (add:i64   a:i64 b:i64)    (+ a b))
(define-macro (inc x) `(+ ,x 1))
(#inline asm ```
  mov $60, %rax
  mov $42, %rdi
  syscall
```)
(add (inc 19)22)
EOF
expect_ok_command "-fmt source" \
  "$ROOT/lipi" -fmt "$FMT_SRC"
expect_ok_command "-fmt source idempotent" \
  "$ROOT/lipi" -fmt "$FMT_SRC"
expect_ok_command "-fmt formatted source checks" \
  "$ROOT/lipi" -check "$FMT_SRC"
expect_grep "-fmt preserves comments" ";fmt comment" "$FMT_SRC"
expect_grep "-fmt normalizes function signature" "(define (add:i64 a:i64 b:i64)" "$FMT_SRC"
expect_grep "-fmt preserves reader shorthand" '`(+ ,x 1)' "$FMT_SRC"
expect_grep "-fmt preserves inline block" '```' "$FMT_SRC"

lsp_msg() {
  body=$1
  len=$(printf '%s' "$body" | wc -c | tr -d ' ')
  printf 'Content-Length: %s\r\n\r\n%s' "$len" "$body"
}

LSP_SRC="$TMP/lsp_unknown.lipi"
printf '(+ foo 1)\n' > "$LSP_SRC"
LSP_URI="file://$LSP_SRC"
{
  lsp_msg '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
  lsp_msg '{"jsonrpc":"2.0","method":"initialized","params":{}}'
  lsp_msg "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"$LSP_URI\",\"languageId\":\"lipi\",\"version\":1,\"text\":\"(+ foo 1)\\n\"}}}"
  lsp_msg "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/formatting\",\"params\":{\"textDocument\":{\"uri\":\"$LSP_URI\"},\"options\":{\"tabSize\":2,\"insertSpaces\":true}}}"
  lsp_msg '{"jsonrpc":"2.0","id":3,"method":"shutdown","params":null}'
  lsp_msg '{"jsonrpc":"2.0","method":"exit","params":null}'
} > "$TMP/lsp.in"
if "$ROOT/lipi" -lsp < "$TMP/lsp.in" > "$TMP/lsp.out" 2> "$TMP/lsp.err"; then
  record_pass "-lsp protocol session"
else
  {
    printf 'lipi -lsp failed\n'
    printf '\nstdout:\n'
    cat "$TMP/lsp.out"
    printf '\nstderr:\n'
    cat "$TMP/lsp.err"
  } > "$TMP/detail"
  record_fail "-lsp protocol session" "$TMP/detail"
fi
expect_grep "-lsp initialize capabilities" "documentFormattingProvider" "$TMP/lsp.out"
expect_grep "-lsp publishes diagnostics" "unknown symbol" "$TMP/lsp.out"
expect_grep "-lsp formatting response" '"newText"' "$TMP/lsp.out"

expect_ok_command "-test all" \
  "$ROOT/lipi" -test "$ROOT/tests/test-framework/main.lipi"
expect_grep "-test all reports three tests" "ok 3 tests" "$TMP/out"

expect_ok_command "-test specific" \
  "$ROOT/lipi" -test "$ROOT/tests/test-framework/main.lipi" test-bool
expect_grep "-test specific reports one test" "ok 1 test" "$TMP/out"

if "$ROOT/lipi" -test "$ROOT/tests/test-framework/fail.lipi" >"$TMP/test-fail.out" 2>"$TMP/test-fail.err"; then
  {
    printf 'failing test unexpectedly passed\n'
    cat "$TMP/test-fail.out" "$TMP/test-fail.err"
  } > "$TMP/detail"
  record_fail "-test failure exits nonzero" "$TMP/detail"
else
  record_pass "-test failure exits nonzero"
fi
expect_grep "-test failure reports FAIL" "FAIL 1 test" "$TMP/test-fail.err"

if "$ROOT/lipi" -test "$ROOT/tests/test-framework/bad_signature.lipi" >"$TMP/test-bad.out" 2>"$TMP/test-bad.err"; then
  {
    printf 'bad test signature unexpectedly passed\n'
    cat "$TMP/test-bad.out" "$TMP/test-bad.err"
  } > "$TMP/detail"
  record_fail "-test bad signature exits nonzero" "$TMP/detail"
else
  record_pass "-test bad signature exits nonzero"
fi
expect_grep "-test bad signature diagnostic" "test functions must not take arguments" "$TMP/test-bad.err"

for pattern_file in "$ROOT"/tests/errors/*.err; do
  expect_error "${pattern_file%.err}.lipi" "$pattern_file"
done

printf '\ncompiler test summary: %d passed, %d failed, %d total\n' "$PASSED" "$FAILED" "$TOTAL"
if [ "$FAILED" -ne 0 ]; then
  cat "$REPORT" >&2
  exit 1
fi

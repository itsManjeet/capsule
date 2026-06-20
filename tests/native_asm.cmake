set(test_out "${SRCLANG_BINARY_DIR}/driver-test")
file(REMOVE_RECURSE "${test_out}")
file(MAKE_DIRECTORY "${test_out}")

execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/native_main.src"
  RESULT_VARIABLE run_compile_result
  OUTPUT_VARIABLE run_compile_stdout
  ERROR_VARIABLE run_compile_stderr)

if(NOT run_compile_result EQUAL 42)
  message(FATAL_ERROR "compile and run failed: expected 42, got ${run_compile_result}\n${run_compile_stdout}\n${run_compile_stderr}")
endif()

set(out_bin "${test_out}/native_main")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/native_main.src" -bin "${out_bin}"
  RESULT_VARIABLE bin_result
  OUTPUT_VARIABLE bin_stdout
  ERROR_VARIABLE bin_stderr)

if(NOT bin_result EQUAL 0)
  message(FATAL_ERROR "binary build failed:\n${bin_stdout}\n${bin_stderr}")
endif()

execute_process(
  COMMAND "${out_bin}"
  RESULT_VARIABLE bin_run_result)

if(NOT bin_run_result EQUAL 42)
  message(FATAL_ERROR "binary exit code mismatch: expected 42, got ${bin_run_result}")
endif()

execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/extern_printf.src"
  RESULT_VARIABLE extern_result
  OUTPUT_VARIABLE extern_stdout
  ERROR_VARIABLE extern_stderr)

if(NOT extern_result EQUAL 0)
  message(FATAL_ERROR "extern printf run failed: expected 0, got ${extern_result}\n${extern_stdout}\n${extern_stderr}")
endif()

if(NOT extern_stdout MATCHES "extern printf ok")
  message(FATAL_ERROR "extern printf stdout mismatch:\n${extern_stdout}\n${extern_stderr}")
endif()

execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/inline_asm_empty.src"
  RESULT_VARIABLE inline_empty_result
  OUTPUT_VARIABLE inline_empty_stdout
  ERROR_VARIABLE inline_empty_stderr)

if(NOT inline_empty_result EQUAL 7)
  message(FATAL_ERROR "inline asm empty run failed: expected 7, got ${inline_empty_result}\n${inline_empty_stdout}\n${inline_empty_stderr}")
endif()

set(out_s_dir "${test_out}/asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/native_main.src" -S "${out_s_dir}"
  RESULT_VARIABLE emit_result
  OUTPUT_VARIABLE emit_stdout
  ERROR_VARIABLE emit_stderr)

if(NOT emit_result EQUAL 0)
  message(FATAL_ERROR "assembly emission failed:\n${emit_stdout}\n${emit_stderr}")
endif()

set(out_s "${out_s_dir}/native_main.s")
if(NOT EXISTS "${out_s}")
  message(FATAL_ERROR "expected assembly file was not created: ${out_s}")
endif()

set(out_arm64_dir "${test_out}/arm64-asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/native_main.src" -target arm64 -S "${out_arm64_dir}"
  RESULT_VARIABLE arm64_emit_result
  OUTPUT_VARIABLE arm64_emit_stdout
  ERROR_VARIABLE arm64_emit_stderr)

if(NOT arm64_emit_result EQUAL 0)
  message(FATAL_ERROR "arm64 assembly emission failed:\n${arm64_emit_stdout}\n${arm64_emit_stderr}")
endif()

set(out_arm64_s "${out_arm64_dir}/native_main.s")
if(NOT EXISTS "${out_arm64_s}")
  message(FATAL_ERROR "expected arm64 assembly file was not created: ${out_arm64_s}")
endif()

file(READ "${out_arm64_s}" arm64_assembly)
string(FIND "${arm64_assembly}" ".arch armv8-a" arm64_arch_marker)
string(FIND "${arm64_assembly}" "stp x29, x30, [sp, #-16]!" arm64_frame_marker)
string(FIND "${arm64_assembly}" "bl srclang_sym_add" arm64_call_marker)
if(arm64_arch_marker EQUAL -1 OR arm64_frame_marker EQUAL -1 OR arm64_call_marker EQUAL -1)
  message(FATAL_ERROR "arm64 assembly did not contain expected AArch64 instructions")
endif()

set(out_extern_s_dir "${test_out}/extern-asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/extern_printf.src" -S "${out_extern_s_dir}"
  RESULT_VARIABLE extern_emit_result
  OUTPUT_VARIABLE extern_emit_stdout
  ERROR_VARIABLE extern_emit_stderr)

if(NOT extern_emit_result EQUAL 0)
  message(FATAL_ERROR "extern assembly emission failed:\n${extern_emit_stdout}\n${extern_emit_stderr}")
endif()

file(READ "${out_extern_s_dir}/extern_printf.s" extern_assembly)
string(FIND "${extern_assembly}" ".extern printf" extern_marker)
string(FIND "${extern_assembly}" "call printf" extern_call_marker)
string(FIND "${extern_assembly}" "srclang_sym_printf" extern_bad_marker)
string(FIND "${extern_assembly}" "xor eax, eax" extern_variadic_marker)
if(extern_marker EQUAL -1 OR extern_call_marker EQUAL -1 OR NOT extern_bad_marker EQUAL -1 OR extern_variadic_marker EQUAL -1)
  message(FATAL_ERROR "extern printf assembly did not contain expected external variadic call")
endif()

set(out_extern_arm64_s_dir "${test_out}/extern-arm64-asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/extern_printf.src" -target arm64 -S "${out_extern_arm64_s_dir}"
  RESULT_VARIABLE extern_arm64_emit_result
  OUTPUT_VARIABLE extern_arm64_emit_stdout
  ERROR_VARIABLE extern_arm64_emit_stderr)

if(NOT extern_arm64_emit_result EQUAL 0)
  message(FATAL_ERROR "extern arm64 assembly emission failed:\n${extern_arm64_emit_stdout}\n${extern_arm64_emit_stderr}")
endif()

file(READ "${out_extern_arm64_s_dir}/extern_printf.s" extern_arm64_assembly)
string(FIND "${extern_arm64_assembly}" ".extern printf" extern_arm64_marker)
string(FIND "${extern_arm64_assembly}" "bl printf" extern_arm64_call_marker)
if(extern_arm64_marker EQUAL -1 OR extern_arm64_call_marker EQUAL -1)
  message(FATAL_ERROR "extern printf arm64 assembly did not contain expected external call")
endif()

set(out_inline_x64_s_dir "${test_out}/inline-x64-asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/inline_asm_x64.src" -target x64 -S "${out_inline_x64_s_dir}"
  RESULT_VARIABLE inline_x64_emit_result
  OUTPUT_VARIABLE inline_x64_emit_stdout
  ERROR_VARIABLE inline_x64_emit_stderr)

if(NOT inline_x64_emit_result EQUAL 0)
  message(FATAL_ERROR "inline x64 assembly emission failed:\n${inline_x64_emit_stdout}\n${inline_x64_emit_stderr}")
endif()

file(READ "${out_inline_x64_s_dir}/inline_asm_x64.s" inline_x64_assembly)
string(FIND "${inline_x64_assembly}" "srclang inline asm begin" inline_x64_begin_marker)
string(FIND "${inline_x64_assembly}" "mov rax, rax" inline_x64_body_marker)
if(inline_x64_begin_marker EQUAL -1 OR inline_x64_body_marker EQUAL -1)
  message(FATAL_ERROR "inline x64 assembly did not contain expected multiline asm")
endif()

set(out_inline_arm64_s_dir "${test_out}/inline-arm64-asm")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/inline_asm_arm64.src" -target arm64 -S "${out_inline_arm64_s_dir}"
  RESULT_VARIABLE inline_arm64_emit_result
  OUTPUT_VARIABLE inline_arm64_emit_stdout
  ERROR_VARIABLE inline_arm64_emit_stderr)

if(NOT inline_arm64_emit_result EQUAL 0)
  message(FATAL_ERROR "inline arm64 assembly emission failed:\n${inline_arm64_emit_stdout}\n${inline_arm64_emit_stderr}")
endif()

file(READ "${out_inline_arm64_s_dir}/inline_asm_arm64.s" inline_arm64_assembly)
string(FIND "${inline_arm64_assembly}" "srclang inline asm begin" inline_arm64_begin_marker)
string(FIND "${inline_arm64_assembly}" "mov x0, x0" inline_arm64_body_marker)
if(inline_arm64_begin_marker EQUAL -1 OR inline_arm64_body_marker EQUAL -1)
  message(FATAL_ERROR "inline arm64 assembly did not contain expected multiline asm")
endif()

set(out_exe "${test_out}/native_from_asm")
execute_process(
  COMMAND "${SRCLANG_CC}" "-no-pie" "${out_s}" "-o" "${out_exe}"
  RESULT_VARIABLE cc_result
  OUTPUT_VARIABLE cc_stdout
  ERROR_VARIABLE cc_stderr)

if(NOT cc_result EQUAL 0)
  message(FATAL_ERROR "assembly compile failed:\n${cc_stdout}\n${cc_stderr}")
endif()

execute_process(
  COMMAND "${out_exe}"
  RESULT_VARIABLE run_result)

if(NOT run_result EQUAL 42)
  message(FATAL_ERROR "native executable exit code mismatch: expected 42, got ${run_result}")
endif()

set(out_o_dir "${test_out}/obj")
execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/native_main.src" -object "${out_o_dir}"
  RESULT_VARIABLE object_result
  OUTPUT_VARIABLE object_stdout
  ERROR_VARIABLE object_stderr)

if(NOT object_result EQUAL 0)
  message(FATAL_ERROR "object emission failed:\n${object_stdout}\n${object_stderr}")
endif()

if(NOT EXISTS "${out_o_dir}/native_main.o")
  message(FATAL_ERROR "expected object file was not created: ${out_o_dir}/native_main.o")
endif()

execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/library.src" -archive "${test_out}/libsrclang_test.a"
  RESULT_VARIABLE archive_result
  OUTPUT_VARIABLE archive_stdout
  ERROR_VARIABLE archive_stderr)

if(NOT archive_result EQUAL 0)
  message(FATAL_ERROR "archive build failed:\n${archive_stdout}\n${archive_stderr}")
endif()

if(NOT EXISTS "${test_out}/libsrclang_test.a")
  message(FATAL_ERROR "expected archive was not created")
endif()

execute_process(
  COMMAND "${SRCLANG_CLI}" "${SRCLANG_SOURCE_DIR}/tests/library.src" -lib "${test_out}/libsrclang_test.so"
  RESULT_VARIABLE lib_result
  OUTPUT_VARIABLE lib_stdout
  ERROR_VARIABLE lib_stderr)

if(NOT lib_result EQUAL 0)
  message(FATAL_ERROR "dynamic library build failed:\n${lib_stdout}\n${lib_stderr}")
endif()

if(NOT EXISTS "${test_out}/libsrclang_test.so")
  message(FATAL_ERROR "expected dynamic library was not created")
endif()

#include "asm_arm64.h"
#include "asm_x64.h"
#include "context.h"
#include "module.h"
#include "native_bytecode.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
  DRIVER_MODE_RUN,
  DRIVER_MODE_BIN,
  DRIVER_MODE_LIB,
  DRIVER_MODE_ARCHIVE,
  DRIVER_MODE_OBJECT,
  DRIVER_MODE_ASSEMBLY
} srclang_driver_mode_t;

typedef enum {
  DRIVER_TARGET_X64,
  DRIVER_TARGET_ARM64
} srclang_driver_target_t;

typedef struct {
  char** values;
  int count;
  int capacity;
} srclang_string_list_t;

typedef struct {
  srclang_driver_mode_t mode;
  srclang_driver_target_t target;
  const char* output;
  const char* assembler;
  const char* assembler_flags;
  const char* linker;
  const char* linker_flags;
  srclang_string_list_t inputs;
} srclang_driver_config_t;

static void list_init(srclang_string_list_t* list) {
  list->values = NULL;
  list->count = 0;
  list->capacity = 0;
}

static char* copy_cstr(const char* value) {
  size_t length = strlen(value);
  char* copy = (char*)malloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, value, length + 1);
  return copy;
}

static void list_push_owned(srclang_string_list_t* list, char* value) {
  if (list->capacity < list->count + 1) {
    int old_capacity = list->capacity;
    list->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    char** grown = (char**)realloc(list->values, sizeof(char*) * (size_t)list->capacity);
    if (grown == NULL) abort();
    list->values = grown;
  }
  list->values[list->count++] = value;
}

static void list_push_copy(srclang_string_list_t* list, const char* value) {
  list_push_owned(list, copy_cstr(value));
}

static void list_free(srclang_string_list_t* list) {
  for (int i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  list_init(list);
}

static bool list_contains(const srclang_string_list_t* list, const char* value) {
  for (int i = 0; i < list->count; i++) {
    if (strcmp(list->values[i], value) == 0) return true;
  }
  return false;
}

static int append_split_flags(srclang_string_list_t* list, const char* flags) {
  if (flags == NULL || flags[0] == '\0') return 0;

  const char* cursor = flags;
  while (*cursor != '\0') {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') break;

    size_t capacity = 32;
    size_t count = 0;
    char* word = (char*)malloc(capacity);
    if (word == NULL) abort();

    char quote = '\0';
    while (*cursor != '\0') {
      char c = *cursor++;
      if (quote == '\0' && isspace((unsigned char)c)) break;
      if ((c == '\'' || c == '"') && quote == '\0') {
        quote = c;
        continue;
      }
      if (quote != '\0' && c == quote) {
        quote = '\0';
        continue;
      }
      if (c == '\\' && *cursor != '\0') c = *cursor++;

      if (capacity < count + 2) {
        capacity *= 2;
        char* grown = (char*)realloc(word, capacity);
        if (grown == NULL) abort();
        word = grown;
      }
      word[count++] = c;
    }

    if (quote != '\0') {
      fprintf(stderr, "Unterminated quote in flags: %s\n", flags);
      free(word);
      return 64;
    }

    word[count] = '\0';
    list_push_owned(list, word);
  }

  return 0;
}

static void usage(void) {
  fprintf(stderr,
      "Usage:\n"
      "  srclang <filenames...>\n"
      "  srclang <filenames...> -bin <bin>\n"
      "  srclang <filenames...> -lib <lib>\n"
      "  srclang <filenames...> -archive <lib>\n"
      "  srclang <filenames...> -object <out-dir>\n"
      "  srclang <filenames...> -S <out-dir>\n"
      "\n"
      "Tool flags:\n"
      "  -target <target>      native, x64, x86_64, arm64, or aarch64\n"
      "  -as <assembler>       assembler command, default cc\n"
      "  -asflags <flags>      assembler flags\n"
      "  -ld <linker>          linker command, default cc\n"
      "  -ldflags <flags>      linker flags\n");
}

static srclang_driver_target_t host_target(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
  return DRIVER_TARGET_ARM64;
#else
  return DRIVER_TARGET_X64;
#endif
}

static const char* target_name(srclang_driver_target_t target) {
  switch (target) {
    case DRIVER_TARGET_X64: return "x64";
    case DRIVER_TARGET_ARM64: return "arm64";
  }
  return "unknown";
}

static bool parse_target(const char* value, srclang_driver_target_t* target) {
  if (strcmp(value, "native") == 0) {
    *target = host_target();
    return true;
  }
  if (strcmp(value, "x64") == 0 || strcmp(value, "x86_64") == 0 || strcmp(value, "amd64") == 0) {
    *target = DRIVER_TARGET_X64;
    return true;
  }
  if (strcmp(value, "arm64") == 0 || strcmp(value, "aarch64") == 0) {
    *target = DRIVER_TARGET_ARM64;
    return true;
  }
  return false;
}

static int set_mode(srclang_driver_config_t* config, srclang_driver_mode_t mode, const char* output) {
  if (config->mode != DRIVER_MODE_RUN || config->output != NULL) {
    fprintf(stderr, "Only one output mode can be selected.\n");
    return 64;
  }
  config->mode = mode;
  config->output = output;
  return 0;
}

static int parse_args(int argc, char** argv, srclang_driver_config_t* config) {
  config->mode = DRIVER_MODE_RUN;
  config->target = host_target();
  config->output = NULL;
  config->assembler = "cc";
  config->assembler_flags = NULL;
  config->linker = "cc";
  config->linker_flags = NULL;
  list_init(&config->inputs);

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      usage();
      return 1;
    }

    if (strcmp(arg, "-bin") == 0 || strcmp(arg, "-lib") == 0 ||
        strcmp(arg, "-archive") == 0 || strcmp(arg, "-object") == 0 ||
        strcmp(arg, "-S") == 0 || strcmp(arg, "-target") == 0 ||
        strcmp(arg, "-as") == 0 ||
        strcmp(arg, "-asflags") == 0 || strcmp(arg, "-ld") == 0 ||
        strcmp(arg, "-ldflags") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s requires a value.\n", arg);
        return 64;
      }
      const char* value = argv[++i];
      if (strcmp(arg, "-bin") == 0) {
        int result = set_mode(config, DRIVER_MODE_BIN, value);
        if (result != 0) return result;
      } else if (strcmp(arg, "-lib") == 0) {
        int result = set_mode(config, DRIVER_MODE_LIB, value);
        if (result != 0) return result;
      } else if (strcmp(arg, "-archive") == 0) {
        int result = set_mode(config, DRIVER_MODE_ARCHIVE, value);
        if (result != 0) return result;
      } else if (strcmp(arg, "-object") == 0) {
        int result = set_mode(config, DRIVER_MODE_OBJECT, value);
        if (result != 0) return result;
      } else if (strcmp(arg, "-S") == 0) {
        int result = set_mode(config, DRIVER_MODE_ASSEMBLY, value);
        if (result != 0) return result;
      } else if (strcmp(arg, "-target") == 0) {
        if (!parse_target(value, &config->target)) {
          fprintf(stderr, "Unknown target '%s'. Expected native, x64, x86_64, arm64, or aarch64.\n", value);
          return 64;
        }
      } else if (strcmp(arg, "-as") == 0) {
        config->assembler = value;
      } else if (strcmp(arg, "-asflags") == 0) {
        config->assembler_flags = value;
      } else if (strcmp(arg, "-ld") == 0) {
        config->linker = value;
      } else {
        config->linker_flags = value;
      }
      continue;
    }

    if (arg[0] == '-') {
      fprintf(stderr, "Unknown flag '%s'.\n", arg);
      return 64;
    }

    list_push_copy(&config->inputs, arg);
  }

  if (config->inputs.count == 0) {
    usage();
    return 64;
  }

  return 0;
}

static int write_file(const char* path, const char* contents) {
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", path, strerror(errno));
    return 74;
  }

  size_t length = strlen(contents);
  size_t written = fwrite(contents, 1, length, file);
  if (fclose(file) != 0 || written != length) {
    fprintf(stderr, "Could not write '%s'.\n", path);
    return 74;
  }

  return 0;
}

static int mkdir_p(const char* path) {
  if (path == NULL || path[0] == '\0') return 0;

  char* copy = copy_cstr(path);
  size_t length = strlen(copy);
  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';

  for (char* cursor = copy + 1; *cursor != '\0'; cursor++) {
    if (*cursor != '/') continue;
    *cursor = '\0';
    if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
      fprintf(stderr, "Could not create directory '%s': %s\n", copy, strerror(errno));
      free(copy);
      return 74;
    }
    *cursor = '/';
  }

  if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "Could not create directory '%s': %s\n", copy, strerror(errno));
    free(copy);
    return 74;
  }

  free(copy);
  return 0;
}

static int mkdir_parent_for_file(const char* path) {
  const char* slash = strrchr(path, '/');
  if (slash == NULL || slash == path) return 0;

  char* parent = (char*)malloc((size_t)(slash - path) + 1);
  if (parent == NULL) abort();
  memcpy(parent, path, (size_t)(slash - path));
  parent[slash - path] = '\0';
  int result = mkdir_p(parent);
  free(parent);
  return result;
}

static char* path_join(const char* left, const char* right) {
  size_t left_length = strlen(left);
  size_t right_length = strlen(right);
  bool slash = left_length > 0 && left[left_length - 1] == '/';
  char* out = (char*)malloc(left_length + (slash ? 0 : 1) + right_length + 1);
  if (out == NULL) abort();
  memcpy(out, left, left_length);
  size_t offset = left_length;
  if (!slash) out[offset++] = '/';
  memcpy(out + offset, right, right_length);
  out[offset + right_length] = '\0';
  return out;
}

static char* sanitized_stem(const char* path, int index) {
  const char* base = strrchr(path, '/');
  base = base == NULL ? path : base + 1;
  const char* dot = strrchr(base, '.');
  size_t length = dot == NULL || dot == base ? strlen(base) : (size_t)(dot - base);
  if (length == 0) length = strlen(base);

  char suffix[32];
  snprintf(suffix, sizeof(suffix), "_%d", index);
  size_t suffix_length = index == 0 ? 0 : strlen(suffix);

  char* out = (char*)malloc(length + suffix_length + 1);
  if (out == NULL) abort();
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)base[i];
    out[i] = (isalnum(c) || c == '_') ? (char)c : '_';
  }
  if (suffix_length > 0) memcpy(out + length, suffix, suffix_length);
  out[length + suffix_length] = '\0';
  return out;
}

static char* output_path_for_input(const char* out_dir, const char* input, const char* extension, int index) {
  char* stem = sanitized_stem(input, index);
  size_t stem_length = strlen(stem);
  size_t extension_length = strlen(extension);
  char* filename = (char*)malloc(stem_length + extension_length + 1);
  if (filename == NULL) abort();
  memcpy(filename, stem, stem_length);
  memcpy(filename + stem_length, extension, extension_length + 1);
  char* path = path_join(out_dir, filename);
  free(filename);
  free(stem);
  return path;
}

static char* unique_output_path_for_input(
    const char* out_dir,
    const char* input,
    const char* extension,
    srclang_string_list_t* used_paths) {
  for (int index = 0; index < 1000000; index++) {
    char* path = output_path_for_input(out_dir, input, extension, index);
    if (!list_contains(used_paths, path)) {
      list_push_copy(used_paths, path);
      return path;
    }
    free(path);
  }

  fprintf(stderr, "Could not allocate a unique output path for '%s'.\n", input);
  return NULL;
}

static char* make_temp_dir(void) {
  for (int attempt = 0; attempt < 100; attempt++) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "/tmp/srclang-%ld-%d", (long)getpid(), attempt);
    if (mkdir(buffer, 0700) == 0) return copy_cstr(buffer);
    if (errno != EEXIST) {
      fprintf(stderr, "Could not create temporary directory '%s': %s\n", buffer, strerror(errno));
      return NULL;
    }
  }

  fprintf(stderr, "Could not create temporary directory.\n");
  return NULL;
}

static int run_command(const srclang_string_list_t* command) {
  char** argv = (char**)calloc((size_t)command->count + 1, sizeof(char*));
  if (argv == NULL) abort();
  for (int i = 0; i < command->count; i++) argv[i] = command->values[i];

  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Could not fork: %s\n", strerror(errno));
    free(argv);
    return 74;
  }

  if (pid == 0) {
    execvp(argv[0], argv);
    fprintf(stderr, "Could not execute '%s': %s\n", argv[0], strerror(errno));
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    fprintf(stderr, "Could not wait for '%s': %s\n", argv[0], strerror(errno));
    free(argv);
    return 74;
  }

  free(argv);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 70;
}

static const char* command_basename(const char* command) {
  const char* slash = strrchr(command, '/');
  return slash == NULL ? command : slash + 1;
}

static bool is_plain_assembler(const char* assembler) {
  const char* base = command_basename(assembler);
  return strcmp(base, "as") == 0;
}

static bool is_compiler_driver(const char* linker) {
  const char* base = command_basename(linker);
  return strcmp(base, "cc") == 0 || strcmp(base, "gcc") == 0 ||
         strcmp(base, "clang") == 0 || strcmp(base, "clang++") == 0 ||
         strcmp(base, "c++") == 0 || strcmp(base, "g++") == 0;
}

static int assemble_file(const srclang_driver_config_t* config, const char* assembly_path, const char* object_path) {
  srclang_string_list_t command;
  list_init(&command);
  list_push_copy(&command, config->assembler);
  if (!is_plain_assembler(config->assembler)) list_push_copy(&command, "-c");
  int split_result = append_split_flags(&command, config->assembler_flags);
  if (split_result != 0) {
    list_free(&command);
    return split_result;
  }
  list_push_copy(&command, assembly_path);
  list_push_copy(&command, "-o");
  list_push_copy(&command, object_path);

  int result = run_command(&command);
  list_free(&command);
  return result;
}

static int link_executable(
    const srclang_driver_config_t* config,
    const srclang_string_list_t* objects,
    const char* output_path) {
  srclang_string_list_t command;
  list_init(&command);
  list_push_copy(&command, config->linker);
  if (is_compiler_driver(config->linker)) list_push_copy(&command, "-no-pie");
  for (int i = 0; i < objects->count; i++) list_push_copy(&command, objects->values[i]);
  list_push_copy(&command, "-o");
  list_push_copy(&command, output_path);
  int split_result = append_split_flags(&command, config->linker_flags);
  if (split_result != 0) {
    list_free(&command);
    return split_result;
  }

  int result = run_command(&command);
  list_free(&command);
  return result;
}

static int link_shared_library(
    const srclang_driver_config_t* config,
    const srclang_string_list_t* objects,
    const char* output_path) {
  srclang_string_list_t command;
  list_init(&command);
  list_push_copy(&command, config->linker);
  list_push_copy(&command, "-shared");
  for (int i = 0; i < objects->count; i++) list_push_copy(&command, objects->values[i]);
  list_push_copy(&command, "-o");
  list_push_copy(&command, output_path);
  int split_result = append_split_flags(&command, config->linker_flags);
  if (split_result != 0) {
    list_free(&command);
    return split_result;
  }

  int result = run_command(&command);
  list_free(&command);
  return result;
}

static int create_archive(const srclang_string_list_t* objects, const char* output_path) {
  srclang_string_list_t command;
  list_init(&command);
  list_push_copy(&command, "ar");
  list_push_copy(&command, "rcs");
  list_push_copy(&command, output_path);
  for (int i = 0; i < objects->count; i++) list_push_copy(&command, objects->values[i]);

  int result = run_command(&command);
  list_free(&command);
  return result;
}

static bool compile_ast_to_assembly(
    srclang_context_t ctx,
    srclang_ast_program_t* ast,
    const char* module_name,
    bool emit_entry,
    srclang_driver_target_t target,
    char** assembly) {
  srclang_native_program_t program;
  if (!srclang_native_compile_program_with_entry(ctx, ast, module_name, emit_entry, &program)) {
    srclang_native_program_free(&program);
    return false;
  }

  switch (target) {
    case DRIVER_TARGET_X64:
      *assembly = srclang_x64_emit_program(ctx, &program);
      break;
    case DRIVER_TARGET_ARM64:
      *assembly = srclang_arm64_emit_program(ctx, &program);
      break;
  }
  bool ok = !srclang_context_has_error(ctx);
  srclang_native_program_free(&program);
  return ok;
}

static bool compile_source_to_assembly(
    srclang_context_t ctx,
    const char* source,
    const char* origin_path,
    const char* module_name,
    bool emit_entry,
    srclang_driver_target_t target,
    char** assembly) {
  srclang_ast_program_t ast;
  if (!srclang_parse_source_with_imports(ctx, source, origin_path, &ast)) return false;
  bool ok = compile_ast_to_assembly(ctx, &ast, module_name, emit_entry, target, assembly);
  srclang_ast_program_free(&ast);
  return ok;
}

static int compile_path_to_assembly_file(
    srclang_context_t ctx,
    const char* input_path,
    const char* output_path,
    bool emit_entry,
    srclang_driver_target_t target) {
  srclang_context_clear_error(ctx);
  char* source = srclang_read_file(ctx, input_path);
  if (source == NULL) {
    fprintf(stderr, "%s\n", srclang_context_last_error(ctx));
    return 74;
  }

  char* module_name = sanitized_stem(input_path, 0);
  char* assembly = NULL;
  bool ok = compile_source_to_assembly(ctx, source, input_path, module_name, emit_entry, target, &assembly);
  free(module_name);
  free(source);

  if (!ok || srclang_context_has_error(ctx)) {
    fprintf(stderr, "%s\n", srclang_context_last_error(ctx));
    free(assembly);
    return 70;
  }

  int result = write_file(output_path, assembly);
  free(assembly);
  return result;
}

static bool parse_inputs_combined(
    srclang_context_t ctx,
    const srclang_string_list_t* inputs,
    srclang_ast_program_t* combined) {
  srclang_ast_program_init(combined);

  for (int i = 0; i < inputs->count; i++) {
    const char* input = inputs->values[i];
    char* source = srclang_read_file(ctx, input);
    if (source == NULL) {
      srclang_ast_program_free(combined);
      return false;
    }

    srclang_ast_program_t unit;
    bool ok = srclang_parse_source_with_imports(ctx, source, input, &unit);
    free(source);
    if (!ok) {
      srclang_ast_program_free(combined);
      return false;
    }

    for (int j = 0; j < unit.count; j++) {
      srclang_ast_program_write(combined, unit.declarations[j]);
      unit.declarations[j] = NULL;
    }
    srclang_ast_program_free(&unit);
  }

  return true;
}

static int compile_inputs_to_assembly_file(
    srclang_context_t ctx,
    const srclang_string_list_t* inputs,
    const char* output_path,
    bool emit_entry,
    srclang_driver_target_t target) {
  srclang_context_clear_error(ctx);

  srclang_ast_program_t combined;
  if (!parse_inputs_combined(ctx, inputs, &combined)) {
    fprintf(stderr, "%s\n", srclang_context_last_error(ctx));
    return 70;
  }

  char* assembly = NULL;
  bool ok = compile_ast_to_assembly(ctx, &combined, "main", emit_entry, target, &assembly);
  srclang_ast_program_free(&combined);

  if (!ok || srclang_context_has_error(ctx)) {
    fprintf(stderr, "%s\n", srclang_context_last_error(ctx));
    free(assembly);
    return 70;
  }

  int result = write_file(output_path, assembly);
  free(assembly);
  return result;
}

static int compile_each_to_assembly(srclang_context_t ctx, const srclang_driver_config_t* config) {
  int result = mkdir_p(config->output);
  if (result != 0) return result;

  srclang_string_list_t used_paths;
  list_init(&used_paths);
  for (int i = 0; i < config->inputs.count; i++) {
    char* output_path = unique_output_path_for_input(config->output, config->inputs.values[i], ".s", &used_paths);
    if (output_path == NULL) {
      list_free(&used_paths);
      return 74;
    }
    result = compile_path_to_assembly_file(ctx, config->inputs.values[i], output_path, true, config->target);
    free(output_path);
    if (result != 0) {
      list_free(&used_paths);
      return result;
    }
  }

  list_free(&used_paths);
  return 0;
}

static int compile_each_to_object(srclang_context_t ctx, const srclang_driver_config_t* config) {
  int result = mkdir_p(config->output);
  if (result != 0) return result;

  char* temp_dir = make_temp_dir();
  if (temp_dir == NULL) return 74;

  srclang_string_list_t used_assembly_paths;
  srclang_string_list_t used_object_paths;
  list_init(&used_assembly_paths);
  list_init(&used_object_paths);

  for (int i = 0; i < config->inputs.count; i++) {
    char* assembly_path = unique_output_path_for_input(temp_dir, config->inputs.values[i], ".s", &used_assembly_paths);
    char* object_path = unique_output_path_for_input(config->output, config->inputs.values[i], ".o", &used_object_paths);
    if (assembly_path == NULL || object_path == NULL) {
      free(assembly_path);
      free(object_path);
      list_free(&used_assembly_paths);
      list_free(&used_object_paths);
      rmdir(temp_dir);
      free(temp_dir);
      return 74;
    }

    result = compile_path_to_assembly_file(ctx, config->inputs.values[i], assembly_path, true, config->target);
    if (result == 0) result = assemble_file(config, assembly_path, object_path);

    remove(assembly_path);
    free(assembly_path);
    free(object_path);
    if (result != 0) {
      list_free(&used_assembly_paths);
      list_free(&used_object_paths);
      rmdir(temp_dir);
      free(temp_dir);
      return result;
    }
  }

  list_free(&used_assembly_paths);
  list_free(&used_object_paths);
  rmdir(temp_dir);
  free(temp_dir);
  return 0;
}

static int compile_combined_to_object(
    srclang_context_t ctx,
    const srclang_driver_config_t* config,
    const char* assembly_path,
    const char* object_path,
    bool emit_entry) {
  int result = compile_inputs_to_assembly_file(ctx, &config->inputs, assembly_path, emit_entry, config->target);
  if (result != 0) return result;
  return assemble_file(config, assembly_path, object_path);
}

static int build_binary_or_run(srclang_context_t ctx, const srclang_driver_config_t* config, bool run_after_link) {
  if (run_after_link && config->target != host_target()) {
    fprintf(stderr, "Cannot run %s output on this host. Use -bin, -object, -lib, -archive, or -S for cross-target output.\n", target_name(config->target));
    return 64;
  }

  char* temp_dir = make_temp_dir();
  if (temp_dir == NULL) return 74;

  char* assembly_path = path_join(temp_dir, "main.s");
  char* object_path = path_join(temp_dir, "main.o");
  char* binary_path = run_after_link ? path_join(temp_dir, "a.out") : copy_cstr(config->output);

  int result = run_after_link ? 0 : mkdir_parent_for_file(binary_path);
  if (result == 0) result = compile_combined_to_object(ctx, config, assembly_path, object_path, true);
  if (result == 0) {
    srclang_string_list_t objects;
    list_init(&objects);
    list_push_copy(&objects, object_path);
    result = link_executable(config, &objects, binary_path);
    list_free(&objects);
  }

  if (result == 0 && run_after_link) {
    srclang_string_list_t command;
    list_init(&command);
    list_push_copy(&command, binary_path);
    result = run_command(&command);
    list_free(&command);
  }

  remove(assembly_path);
  remove(object_path);
  if (run_after_link) remove(binary_path);
  rmdir(temp_dir);
  free(assembly_path);
  free(object_path);
  free(binary_path);
  free(temp_dir);
  return result;
}

static int build_library(srclang_context_t ctx, const srclang_driver_config_t* config, bool archive) {
  char* temp_dir = make_temp_dir();
  if (temp_dir == NULL) return 74;

  char* assembly_path = path_join(temp_dir, archive ? "archive.s" : "library.s");
  char* object_path = path_join(temp_dir, archive ? "archive.o" : "library.o");

  int result = mkdir_parent_for_file(config->output);
  if (result == 0) result = compile_combined_to_object(ctx, config, assembly_path, object_path, false);
  if (result == 0) {
    srclang_string_list_t objects;
    list_init(&objects);
    list_push_copy(&objects, object_path);
    result = archive ? create_archive(&objects, config->output) : link_shared_library(config, &objects, config->output);
    list_free(&objects);
  }

  remove(assembly_path);
  remove(object_path);
  rmdir(temp_dir);
  free(assembly_path);
  free(object_path);
  free(temp_dir);
  return result;
}

static int run_driver(srclang_context_t ctx, const srclang_driver_config_t* config) {
  switch (config->mode) {
    case DRIVER_MODE_RUN:
      return build_binary_or_run(ctx, config, true);
    case DRIVER_MODE_BIN:
      return build_binary_or_run(ctx, config, false);
    case DRIVER_MODE_LIB:
      return build_library(ctx, config, false);
    case DRIVER_MODE_ARCHIVE:
      return build_library(ctx, config, true);
    case DRIVER_MODE_OBJECT:
      return compile_each_to_object(ctx, config);
    case DRIVER_MODE_ASSEMBLY:
      return compile_each_to_assembly(ctx, config);
  }
  return 64;
}

int main(int argc, char** argv) {
  srclang_driver_config_t config;
  int parse_result = parse_args(argc, argv, &config);
  if (parse_result != 0) {
    if (parse_result == 1) {
      list_free(&config.inputs);
      return 0;
    }
    list_free(&config.inputs);
    return parse_result;
  }

  srclang_context_t ctx = srclang_context_new();
  int result = run_driver(ctx, &config);
  srclang_context_free(ctx);
  list_free(&config.inputs);
  return result;
}

#include <utility>
#include <fstream>
#include <dlfcn.h>
#include "Language.hxx"
#include "Builtin.hxx"


using namespace srclang;

Language::Language()
        : globals(65536),
          options({
                          {"VERSION", SRCLANG_VERSION},
                          {"GC_HEAP_GROW_FACTOR",   1.0f},
                          {"GC_INITIAL_TRIGGER",    200},
                          {"SEARCH_PATH",           std::string(getenv("HOME")) +
                                                    "/.local/share/srclang/:/usr/share/srclang:/usr/lib/srclang/"},
                          {"LOAD_LIBC",             true},
                          {"LIBC",                  "libc.so.6"},
                          {"C_LIBRARY",             ""},
                          {"C_INCLUDE",             ""},
                          {"EXPERIMENTAL_FEATURES", false},
                          {"DEBUG",                 false},
                          {"BREAK",                 false},
                  }) {

    for (auto b: builtins) {
        memoryManager.heap.push_back(b);
    }
    {
        int i = 0;
#define X(id) symbolTable.define(#id, i++);
        SRCLANG_BUILTIN_LIST
#undef X
    }

    symbolTable.define("__FILE__");

    define("true", SRCLANG_VALUE_TRUE);
    define("false", SRCLANG_VALUE_FALSE);
    define("null", SRCLANG_VALUE_NULL);
}

void Language::define(const std::string &id, Value value) {
    auto symbol = symbolTable.resolve(id);
    if (symbol == std::nullopt) {
        symbol = symbolTable.define(id);
    }
    globals[symbol->index] = value;
}

Value Language::execute(const std::string &input, const std::string &filename) {
    state = tcc_new();
    if (state == nullptr) {
        throw std::runtime_error(strerror(errno));
    }
    tcc_set_output_type(state, TCC_OUTPUT_MEMORY);
    auto compiler = Compiler(input.begin(), input.end(), filename, this);
    if (!compiler.compile()) {
        return SRCLANG_VALUE_ERROR(strdup(compiler.get_error().c_str()));
    }
    if (tcc_compile_string(state, cc_code.c_str()) == -1) {
        return SRCLANG_VALUE_ERROR(strdup("TCC compilation failed"));
    }
    if (tcc_relocate(state, TCC_RELOCATE_AUTO) == -1) {
        return SRCLANG_VALUE_ERROR(strdup("TCC relocation failed"));
    }
    auto code = std::move(compiler.code());
    return execute(code, compiler.debugInfo());
}

Value Language::execute(ByteCode &code, const std::shared_ptr<DebugInfo> &debugInfo) {
    auto interpreter = Interpreter(code, debugInfo, this);
    if (!interpreter.run()) {
        return SRCLANG_VALUE_ERROR(strdup(interpreter.get_error().c_str()));
    }
    return *interpreter.sp;
}

Value Language::execute(const std::filesystem::path &filename) {
    if (!std::filesystem::exists(filename)) {
        return SRCLANG_VALUE_ERROR(strdup(("Missing file " + filename.string()).c_str()));
    }
    std::ifstream reader(filename);

    if (filename.has_extension() && filename.extension() == ".src") {
        std::string input(
                (std::istreambuf_iterator<char>(reader)),
                (std::istreambuf_iterator<char>())
        );
        return execute(input, filename.string());
    } else {
        void *handler = nullptr;
        ByteCode *code = ByteCode::read(reader);
        auto debugInfo = std::shared_ptr<DebugInfo>(DebugInfo::read(reader));
        constants = code->constants;
        return execute(*code, debugInfo);
    }
}

void Language::appendSearchPath(const std::string &path) {
    options["SEARCH_PATH"] = path + ":" + std::get<std::string>(options["SEARCH_PATH"]);
}

Value Language::resolve(const std::string &id) {
    auto symbol = symbolTable.resolve(id);
    if (symbol == std::nullopt) {
        return SRCLANG_VALUE_NULL;
    }
    switch (symbol->scope) {
        case Symbol::GLOBAL:
            return globals[symbol->index];
        default:
            return SRCLANG_VALUE_NULL;
    }
}

bool Language::compile(const std::string &filename, std::optional<std::string> output) {
    std::ifstream reader(filename);
    std::string input(
            (std::istreambuf_iterator<char>(reader)),
            (std::istreambuf_iterator<char>())
    );
    reader.close();

    tcc_set_output_type(state, TCC_OUTPUT_DLL);

    auto compiler = Compiler(input.begin(), input.end(), filename, this);
    if (!compiler.compile()) {
        return false;
    }
    auto code = std::move(compiler.code());
    if (output == std::nullopt) {
        output = filename.substr(0, filename.size() - 4);
    }

    std::ofstream writer(*output);
    code.dump(writer);
    compiler.debugInfo()->dump(writer);
    writer.close();

    if (tcc_compile_string(state, cc_code.c_str()) == -1) {
        return false;
    }

    if (tcc_output_file(state, (filename.substr(0, filename.size() - 4) + ".so").c_str()) == -1) {
        return false;
    }

    return true;
}

Value Language::call(Value callee, const std::vector<Value> &args) {
    ByteCode code;
    code.instructions = std::make_unique<Instructions>();
    code.constants = this->constants;

    code.constants.push_back(callee);
    code.instructions->push_back(static_cast<const unsigned int>(OpCode::CONST));
    code.instructions->push_back(code.constants.size() - 1);

    for (auto arg: args) {
        code.constants.push_back(arg);
        code.instructions->push_back(static_cast<const unsigned int>(OpCode::CONST));
        code.instructions->push_back(code.constants.size() - 1);
    }


    code.instructions->push_back(static_cast<const unsigned int>(OpCode::CALL));
    code.instructions->push_back(args.size());
    code.instructions->push_back(static_cast<const unsigned int>(OpCode::HLT));

    this->constants = code.constants;

    auto interpreter = Interpreter(code, nullptr, this);
    if (!interpreter.run()) {
        return SRCLANG_VALUE_ERROR(strdup("INTERPRETATION FAILED"));
    }
    if (std::distance(interpreter.stack.begin(), interpreter.sp) > 0) {
        return *(interpreter.sp - 1);
    }
    return SRCLANG_VALUE_TRUE;
}

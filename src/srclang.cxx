#include <readline/history.h>
#include <readline/readline.h>

#include <fstream>
#include <utility>

#include "Language.hxx"

using namespace srclang;

const auto LOGO = R"(
                       .__                         
  _____________   ____ |  | _____    ____    ____  
 /  ___/\_  __ \_/ ___\|  | \__  \  /    \  / ___\
 \___ \  |  | \/\  \___|  |__/ __ \|   |  \/ /_/  >
/____  > |__|    \___  >____(____  /___|  /\___  /
     \/              \/          \/     \//_____/

)";

Language *__srclang_global_language;

int run(Language *language, std::optional<std::string> path) {
    if (path == std::nullopt) {
        std::cerr << "No input file specified" << std::endl;
        return 1;
    }
    auto result = language->execute(std::filesystem::path(*path));
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        std::cerr << (char *)SRCLANG_VALUE_AS_OBJECT(result)->pointer << std::endl;
        return 1;
    }
    return 0;
}

bool is_complete(std::string const &input) {
    std::vector<char> stack;
    for (int i = 0; i < input.length(); i++) {
        switch (input[i]) {
            case '{':
            case '[':
            case '(':
                stack.push_back(input[i]);
                break;
            case '}':
                if (stack.back() == '{') stack.pop_back();
                break;
            case ']':
                if (stack.back() == '[') stack.pop_back();
                break;
            case ')':
                if (stack.back() == '(') stack.pop_back();
                break;
        }
    }
    return stack.empty() && input.back() != '\\';
}

int interactive(Language *language) {
    std::cout << LOGO << std::endl;
    std::cout << "Source Programming Language" << std::endl;
    std::cout << "Copyright (C) 2023 rlxos" << std::endl;

    while (true) {
        std::string input = readline((const char *)SRCLANG_VALUE_AS_OBJECT(language->resolve("PS"))->pointer);
        if (input.empty()) continue;

        if (input == ".exit") break;

        while (!is_complete(input)) {
            std::string sub_input = readline("... ");
            input += sub_input;
        }

        add_history(input.c_str());

        auto result = language->execute(input, "<script>");
        std::cout << ":: " << SRCLANG_VALUE_GET_STRING(result) << std::endl;
    }

    return 0;
}

int printHelp() {
    std::cout << LOGO << std::endl;
    std::cout << "Source Programming Language" << std::endl;
    std::cout << "Copyright (C) 2021 rlxos" << std::endl;

    std::cout << "\n"
                 " COMMANDS:"
              << std::endl;
    std::cout << "   run                    Run srclang script and bytecode (source ends with .src)\n"
              << "   interactive            Start srclang interactive shell\n"
              << "   help                   Print this help message\n"
              << '\n'
              << " FLAGS:\n"
              << "  -debug                  Enable debugging outputs\n"
              << "  -ir                     Print IR\n"
              << "  -breakpoint             Enable breakpoint at instructions\n"
              << "  -search-path <path>     Append module search path\n"
              << "  -define <key>=<value>   Define variable from command line\n"
              << std::endl;

    return 1;
}

int main(int argc, char **argv) {
    Language language;

    auto args = new SrcLangList();
    __srclang_global_language = &language;

    std::string task = "interactive";
    std::optional<std::string> filename, output;
    std::filesystem::path project_path = std::filesystem::current_path();

    void *handler = nullptr;

    if (argc >= 2) {
        task = argv[1];
    }

    bool prog_args_start = false;
    std::vector<std::string> cli_args;

    for (int i = 2; i < argc; i++) {
        if (!prog_args_start && strcmp(argv[i], "--") == 0) {
            prog_args_start = true;
            continue;
        }
        if (prog_args_start) {
            args->push_back(SRCLANG_VALUE_STRING(strdup(argv[i])));
            continue;
        }
        if (argv[i][0] == '-') {
            argv[i]++;
#define CHECK_COUNT(c)                                               \
    if (argc <= c + i) {                                             \
        std::cout << "expecting " << c << " arguments" << std::endl; \
        return 1;                                                    \
    }
            if (strcmp(argv[i], "debug") == 0) {
                language.options["DEBUG"] = true;
            } else if (strcmp(argv[i], "breakpoint") == 0) {
                language.options["BREAK"] = true;
            } else if (strcmp(argv[i], "ir") == 0) {
                language.options["IR"] = true;
            } else if (strcmp(argv[i], "define") == 0) {
                CHECK_COUNT(1)
                auto value = std::string(argv[++i]);
                auto idx = value.find_first_of('=');
                if (idx == std::string::npos) {
                    language.define(value, SRCLANG_VALUE_TRUE);
                } else {
                    auto key = value.substr(0, idx);
                    value = value.substr(idx + 1);
                    language.define(key, SRCLANG_VALUE_STRING(strdup(value.c_str())));
                }
            } else if (strcmp(argv[i], "search-path") == 0) {
                CHECK_COUNT(1)
                language.appendSearchPath(argv[++i]);
            } else if (strcmp(argv[i], "o") == 0) {
                CHECK_COUNT(1);
                output = argv[++i];
            } else if (strcmp(argv[i], "project-path") == 0) {
                CHECK_COUNT(1);
                std::string path(argv[++i]);
                if (path[0] == '/') {
                    project_path = path;
                } else {
                    project_path = std::filesystem::canonical(std::filesystem::current_path() / path);
                }
            } else {
                std::cerr << "ERROR: invalid flag '-" << argv[i] << "'" << std::endl;
                return 1;
            }
#undef CHECK_COUNT
        } else if (filename == std::nullopt &&
                   std::filesystem::exists(argv[i])) {
            std::string path(argv[i]);
            if (path[0] == '/') {
                filename = path;
            } else {
                filename = std::filesystem::canonical(std::filesystem::current_path() / path).string();
            }
        } else {
            cli_args.emplace_back(argv[i]);
        }
    }

    language.define("__FILE__", SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING((filename.has_value() ? filename->c_str() : "<script>"))));
    language.define("__ARGS__", SRCLANG_VALUE_LIST(args));
    language.define("PS", SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(">> ")));

    if (task == "help")
        return printHelp();
    else if (task == "run")
        return run(&language, filename);
    else if (task == "interactive")
        return interactive(&language);

    return printHelp();
}
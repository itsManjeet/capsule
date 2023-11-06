#include <fstream>

#include "Language/Language.hxx"

using namespace srclang;

const auto LOGO = R"(
                       .__                         
  _____________   ____ |  | _____    ____    ____  
 /  ___/\_  __ \_/ ___\|  | \__  \  /    \  / ___\
 \___ \  |  | \/\  \___|  |__/ __ \|   |  \/ /_/  >
/____  > |__|    \___  >____(____  /___|  /\___  /
     \/              \/          \/     \//_____/

)";

#ifdef _WIN32
static std::string readline(const char* PS) {
    std::cout << PS;
    std::string line;
    std::getline(std::cin, line, '\n');

    return std::move(line);
}

static void add_history(const char* line) {

}
#else

#include <readline/history.h>
#include <readline/readline.h>

#endif

Language *__srclang_global_language;


int printHelp() {
    std::cout << LOGO << std::endl;
    std::cout << "Source Programming Language" << std::endl;
    std::cout << "Copyright (C) 2021 rlxos" << std::endl
              << "  -debug                  Enable debugging outputs\n"
              << "  -ir                     Print IR\n"
              << "  -breakpoint             Enable breakpoint at instructions\n"
              << "  -search-path <path>     Append module search path\n"
              << "  -define <key>=<value>   Define variable from command line\n"
              << "  -help                   Print this help message\n"
              << std::endl;

    return 1;
}

int main(int argc, char **argv) {
    Language language;

    auto args = new SrcLangList();
    __srclang_global_language = &language;

    std::optional <std::string> filename;
    bool prog_args_start = false;
    bool print_help = false;
    std::vector <std::string> cli_args;

    for (int i = 1; i < argc; i++) {
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
            } else if (strcmp(argv[i], "help") == 0) {
                print_help = true;
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

    language.define("__FILE__", SRCLANG_VALUE_SET_REF(
            SRCLANG_VALUE_STRING((filename.has_value() ? filename->c_str() : "<script>"))));
    language.define("__ARGS__", SRCLANG_VALUE_LIST(args));

    if (print_help)
        return printHelp();

    if (filename) {
        auto result = language.execute(std::filesystem::path(*filename));
        if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
            std::cerr << (char *) SRCLANG_VALUE_AS_OBJECT(result)->pointer << std::endl;
            return 1;
        }
        return 0;
    }

    return printHelp();
}
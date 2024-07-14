#include <filesystem>
#include <fstream>

#include "Interpreter.h"

#ifdef WITH_READLINE
#include <readline/readline.h>
#endif

using namespace SrcLang;

std::string getline(const std::string& prompt) {
    std::string line;
#ifdef WITH_READLINE
    char* l = readline(prompt.c_str());
    line = l;
    free(l);
#else
    std::cout << prompt;
    std::getline(std::cin, line);
#endif
    return line;
}

bool is_complete(const std::string& s) {
    if (s.empty()) return false;
    std::vector<char> stack;
    std::map<char, char> mapping = {{'{', '}'}, {'(', ')'}, {'[', ']'}};
    for (auto ch : s) {
        switch (ch) {
            case '(':
            case '{':
            case '[':
                stack.push_back(ch);
                break;
            case ')':
            case '}':
            case ']':
                if (stack.empty()) return true;
                if (mapping[stack.back()] != ch) return true;
                stack.pop_back();
                break;
        }
    }
    return stack.empty();
}

int main(int argc, char** argv) {
    bool isProgArgs = false;
    bool interactive = false;
    bool dumpAst = false;
    std::optional<std::filesystem::path> filename;
    auto progArgs = new SrcLangList();
    auto interpreter = SrcLang::Interpreter();

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                while (i < argc) {
                    progArgs->emplace_back(SRCLANG_VALUE_STRING(argv[i++]));
                }
            }
            if (strcmp(argv[i], "--debug") == 0)
                interpreter.setOption("DEBUG", true);
            else if (strcmp(argv[i], "--break") == 0)
                interpreter.setOption("BREAK", true);
            else if (strcmp(argv[i], "--search-path") == 0)
                interpreter.appendOption("SEARCH_PATH", argv[i]);
            else if (strcmp(argv[i], "--interactive") == 0)
                interactive = true;
            else if (strcmp(argv[i], "--dump-ast") == 0)
                interpreter.setOption("DUMP_AST", true);
            else {
                std::cerr << "ERROR: invalid flag '" << argv[i] << "'" << std::endl;
                return 1;
            }
        } else if (!filename && !isProgArgs && !interactive) {
            filename = argv[i];
            filename = std::filesystem::current_path() / *filename;
            interactive = false;
        } else {
            progArgs->emplace_back(SRCLANG_VALUE_STRING(strdup(argv[i])));
        }
    }

    std::string source;
    if (filename) {
        std::ifstream reader(*filename);
        source = std::string(
            (std::istreambuf_iterator<char>(reader)),
            (std::istreambuf_iterator<char>()));
    } else {
        interactive = true;
    }

    do {
        if (interactive) {
            source.clear();
            std::string prompt = ">> ";
            while (!is_complete(source)) {
                source += getline(prompt);
                prompt = "... ";
            }
        }

        auto result = interpreter.run(source, filename ? filename->string() : "stdin");
        if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
            std::cerr << SRCLANG_VALUE_AS_ERROR(result) << std::endl;
            if (!interactive) return 1;
        } else if (interactive) {
            std::cout << ":: " << SRCLANG_VALUE_GET_STRING(result) << std::endl;
        } else {
            return 0;
        }

    } while (interactive);
}
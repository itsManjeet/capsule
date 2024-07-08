#include <filesystem>
#include <fstream>

#include "Interpreter.h"

using namespace SrcLang;

int main(int argc, char **argv) {
    bool isProgArgs = false;
    bool debug = false;
    bool breakPoint = false;
    bool interactive = false;
    std::optional<std::filesystem::path> filename;
    auto progArgs = new SrcLangList();

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                while (i < argc) {
                    progArgs->emplace_back(SRCLANG_VALUE_STRING(argv[i++]));
                }
            }
            if (strcmp(argv[i], "--debug") == 0)
                debug = true;
            else if (strcmp(argv[i], "--break") == 0)
                debug = true;
            else if (strcmp(argv[i], "--interactive") == 0)
                interactive = true;
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

    auto interpreter = SrcLang::Interpreter(debug, breakPoint);

    do {
        if (interactive) {
            std::cout << ">> ";
            std::getline(std::cin, source);
        }

        auto result = interpreter.run(source, filename ? *filename : "stdin");
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
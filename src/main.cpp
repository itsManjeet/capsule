#include "Interpreter.h"
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>

#ifdef WITH_READLINE
#    include <readline/readline.h>
#endif

using namespace SrcLang;

std::wstring getline(const std::string& prompt) {
    std::wstring line;
    std::cout << prompt;
    std::getline(std::wcin, line);
    return line;
}

bool is_complete(const std::wstring& s) {
    if (s.empty()) return false;
    std::vector<wchar_t> stack;
    std::map<wchar_t, wchar_t> mapping = {
            {L'{', L'}'}, {L'(', L')'}, {L'[', L']'}};
    for (auto ch : s) {
        switch (ch) {
        case L'(':
        case L'{':
        case L'[': stack.push_back(ch); break;
        case L')':
        case L'}':
        case L']':
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
    std::optional<std::filesystem::path> filename;
    auto prog_args = std::vector<Value>();
    auto interpreter = SrcLang::Interpreter();

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0) {
                while (i < argc) {
                    prog_args.emplace_back(SRCLANG_VALUE_STRING(argv[i++]));
                }
            }
            if (strcmp(argv[i], "--debug") == 0)
                interpreter.setOption(L"DEBUG", true);
            else if (strcmp(argv[i], "--break") == 0)
                interpreter.setOption(L"BREAK", true);
            else if (strcmp(argv[i], "--search-path") == 0)
                interpreter.appendOption(L"SEARCH_PATH", s2ws(argv[i]));
            else if (strcmp(argv[i], "--interactive") == 0)
                interactive = true;
            else if (strcmp(argv[i], "--dump-ast") == 0)
                interpreter.setOption(L"DUMP_AST", true);
            else {
                std::cerr << "ERROR: invalid flag '" << argv[i] << "'"
                          << std::endl;
                return 1;
            }
        } else if (!filename && !isProgArgs && !interactive) {
            filename = argv[i];
            filename = std::filesystem::current_path() / *filename;
            interactive = false;
        } else {
            prog_args.emplace_back(SRCLANG_VALUE_STRING(strdup(argv[i])));
        }
    }

    constexpr char locale_name[] = "";

    setlocale(LC_ALL, locale_name);
    std::locale::global(std::locale(locale_name));
    std::wcin.imbue(std::locale());
    std::wcout.imbue(std::locale());
    std::wcerr.imbue(std::locale());

    std::wstring source;
    if (filename) {
        std::wifstream reader(*filename);
        reader.imbue(
                std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
        std::wstringstream wss;
        wss << reader.rdbuf();
        source = wss.str();
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

        auto result = interpreter.run(source,
                filename ? s2ws(filename->string()) : L"stdin");
        if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
            std::wcerr << SRCLANG_VALUE_AS_ERROR(result) << std::endl;
            if (!interactive) return 1;
        } else if (interactive) {
            std::wcout << L":: " << SRCLANG_VALUE_GET_STRING(result)
                       << std::endl;
        } else {
            return 0;
        }

    } while (interactive);
}
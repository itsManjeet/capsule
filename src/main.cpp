#include "Interpreter.h"
#include <fstream>
#include <filesystem>

using namespace SrcLang;

int main(int argc, char **argv) {
    auto interpreter = SrcLang::Interpreter();

    if (argc == 1) {
        std::cerr << "Usage: " << argv[0] << " <Script>" << std::endl;
        return 1;
    }

    std::filesystem::path filename(argv[1]);
    if (filename.string()[0] != '/') {
        filename = std::filesystem::current_path() / filename;
    }

    std::ifstream reader(filename);
    std::string source(
            (std::istreambuf_iterator<char>(reader)),
            (std::istreambuf_iterator<char>())
    );

    auto result = interpreter.run(source, filename);
    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
        std::cerr << (const char *) SRCLANG_VALUE_AS_OBJECT(result)->pointer << std::endl;
        return 2;
    }

    return 0;
}
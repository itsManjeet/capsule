#include "ProjectManager.hxx"
#include "Language.hxx"
#include <fstream>
#include <utility>

using namespace srclang;

ProjectManager::ProjectManager(Language *language, std::filesystem::path projectPath)
        : language{language},
          projectPath(std::move(projectPath)) {

}

void ProjectManager::load() {
    auto buildConfig = projectPath / "build.src";
    if (!std::filesystem::exists(buildConfig)) {
        throw std::runtime_error("Missing project config '" + buildConfig.string() + "'");
    }

    auto localLang = Language();
    {
        auto status = localLang.execute(buildConfig);
        if (SRCLANG_VALUE_GET_TYPE(status) == ValueType::Error) {
            throw std::runtime_error((const char *) SRCLANG_VALUE_AS_OBJECT(status)->pointer);
        }
    }

    auto readConfig = [&](std::string const &key) -> std::string {
        auto symbol = localLang.symbolTable.resolve(key);
        if (symbol == std::nullopt ||
            symbol->scope != Symbol::GLOBAL) {
            throw std::runtime_error("missing key '" + key + "'");
        }
        auto value = localLang.globals[symbol->index];
        if (SRCLANG_VALUE_GET_TYPE(value) != ValueType::String) {
            throw std::runtime_error("not a string value '" + key + "'");
        }
        return (const char *) SRCLANG_VALUE_AS_OBJECT(value)->pointer;
    };

    auto fallbackConfig = [&](std::string const &key, std::string const &value) -> std::string {
        try {
            return readConfig(key);
        } catch (...) {
            return value;
        }
    };

    config["NAME"] = readConfig("PROJECT_NAME");
    config["VERSION"] = fallbackConfig("PROJECT_VERSION", "0.0.1");
}

void ProjectManager::create(std::string const &projectName) {
    auto buildConfig = projectPath / projectName / "build.src";
    if (std::filesystem::exists(buildConfig)) {
        throw std::runtime_error("Project already exists '" + buildConfig.string() + "'");
    }

    for (auto dir: {"exec", "modules", "data", "tests", "native"}) {
        std::filesystem::create_directories(projectPath / projectName / dir);
    }

    std::ofstream writer(buildConfig);
    writer << "PROJECT_NAME := \"" << projectName << "\";" << std::endl;
}

void ProjectManager::test() {
    load();

    auto testPath = projectPath / "tests";
    if (!std::filesystem::exists(testPath)) {
        std::cout << "no test cases found" << std::endl;
        return;
    }

    int TOTAL_TEST_CASES = 0;
    int TOTAL_FAILED = 0;

    for (auto const &test: std::filesystem::recursive_directory_iterator(testPath)) {
        if (test.is_regular_file() &&
            test.path().has_extension() &&
            test.path().filename().string().starts_with("test_") &&
            test.path().extension() == ".src") {
            Language lang;
            auto result = lang.execute(test.path());
            TOTAL_TEST_CASES++;
            if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error) {
                std::cout << "FAILED : ";
                TOTAL_FAILED++;
            } else {
                std::cout << "PASSED : ";
            }
            std::cout << "     " << test.path() << std::endl;
        }
    }

    std::cout << "\n\n"
              << "--- REPORT --------------\n"
              << "   TOTAL    :    " << TOTAL_TEST_CASES << '\n'
              << "   FAILED   :    " << TOTAL_FAILED << '\n'
              << "   PASSED   :    " << TOTAL_TEST_CASES - TOTAL_FAILED << '\n'
              << std::endl;
}

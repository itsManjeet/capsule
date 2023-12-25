/*
 * Copyright (C) 2023 Manjeet Singh <itsmanjeet1998@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <fstream>
#include "../src/Language/Language.hxx"

using namespace srclang;

int main(int argc, char **argv) {
    if (argc == 1) {
        std::cout << "ERROR: no test file provided" << std::endl;
        return 1;
    }

    std::string test_file = argv[1];
    bool success = true;
    if (std::filesystem::exists(test_file + ".out")) {
        success = true;
    } else if (std::filesystem::exists(test_file + ".err")) {
        success = false;
    } else {
        std::cout << "ERROR: no output file present '" << test_file << ".out'" << std::endl;
        return 1;
    }

    auto language = srclang::Language();
    std::stringstream buffer;
    std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());
    auto result = language.execute(test_file);
    std::cout.rdbuf(old);

    if (SRCLANG_VALUE_GET_TYPE(result) == ValueType::Error && success) {
        throw std::runtime_error("ERROR: Code failed with error " + SRCLANG_VALUE_GET_STRING(result));
    } else {
        std::ifstream reader(test_file + (success ? ".out" : ".err"));
        if (!reader.good()) throw std::runtime_error("failed to read test file");

        std::string expected(
                (std::istreambuf_iterator<char>(reader)),
                (std::istreambuf_iterator<char>())
        );
        auto result_str = success
                          ? buffer.str()
                          : std::string(SRCLANG_VALUE_GET_STRING(result));
        if (!success) {
            result_str = result_str.substr(result_str.find('\n') + 1) + "\n";
        }
        if (expected != result_str) {
            throw std::runtime_error("'" + expected + "' != '" + result_str + "'");
        }
    }

}
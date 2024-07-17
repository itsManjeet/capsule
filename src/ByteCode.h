#ifndef SRCLANG_BYTECODE_H
#define SRCLANG_BYTECODE_H

#include "Instructions.h"
#include "Value.h"
#include <iomanip>
#include <memory>

namespace SrcLang {

struct ByteCode {
    std::unique_ptr<Instructions> instructions;
    std::vector<Value> constants;
    using Iterator = typename std::vector<Value>::iterator;

    static int debug(Instructions const& instructions,
            std::vector<Value> const& constants, int offset, std::wostream& os);

    static void debug(std::wostream& os, Instructions const& instructions,
            std::vector<Value> const& constants) {
        os << L"== CODE ==" << std::endl;
        for (int offset = 0; offset < instructions.size();) {
            offset = ByteCode::debug(instructions, constants, offset, os);
            os << std::endl;
        }
        os << L"\n== CONSTANTS ==" << std::endl;
        for (auto i = 0; i < constants.size(); i++) {
            os << i << L" " << SRCLANG_VALUE_DEBUG(constants[i]) << std::endl;
        }
    }
};

} // namespace SrcLang

#endif // SRCLANG_BYTECODE_H

#ifndef SRCLANG_BYTECODE_H
#define SRCLANG_BYTECODE_H

#include <iomanip>
#include <memory>

#include "Instructions.h"
#include "Value.h"

namespace SrcLang {

struct ByteCode {
    std::unique_ptr<Instructions> instructions;
    std::vector<Value> constants;
    using Iterator = typename std::vector<Value>::iterator;

    static int debug(Instructions const &instructions,
                     std::vector<Value> const &constants, int offset,
                     std::ostream &os);

    static void debug(std::ostream &os,
                      Instructions const &instructions,
                      std::vector<Value> const &constants) {
        os << "== CODE ==" << std::endl;
        for (int offset = 0; offset < instructions.size();) {
            offset = ByteCode::debug(
                instructions, constants, offset, os);
            os << std::endl;
        }
        os << "\n== CONSTANTS ==" << std::endl;
        for (auto i = 0; i < constants.size(); i++) {
            os << i << " " << SRCLANG_VALUE_DEBUG(constants[i])
               << std::endl;
        }
    }
};

}  // namespace SrcLang

#endif  // SRCLANG_BYTECODE_H

#include "ByteCode.h"

#include "SymbolTable.h"

using namespace SrcLang;

int ByteCode::debug(Instructions const& instructions,
        std::vector<Value> const& constants, int offset, std::wostream& os) {
    os << std::setfill(L'0') << std::setw(4) << offset << " ";
    auto op = static_cast<OpCode>(instructions[offset]);
    os << s2ws(SRCLANG_OPCODE_ID[static_cast<int>(op)]);
    offset += 1;
    switch (op) {
    case OpCode::CONST_: {
        auto const pos = instructions[offset++];
        if (!constants.empty()) {
            os << L" " << pos << L" '" << SRCLANG_VALUE_DEBUG(constants[pos])
               << L"'";
        }

    } break;
    case OpCode::INDEX:
    case OpCode::PACK:
    case OpCode::MAP:
        os << " " << static_cast<int>(instructions[offset++]);
        break;

    case OpCode::CONTINUE:
    case OpCode::BREAK:
    case OpCode::JNZ:
    case OpCode::JMP: os << " '" << instructions[offset++] << "'"; break;
    case OpCode::LOAD:
    case OpCode::STORE: {
        auto const scope = instructions[offset++];
        auto const pos = instructions[offset++];
        os << " " << pos << " '" << SRCLANG_SYMBOL_ID[scope] << "'";
    } break;
    case OpCode::CLOSURE:
        os << L" " << constants[instructions[offset++]] << L" "
           << instructions[offset++];
        break;

    case OpCode::CONST_INT:
        os << " '" << SRCLANG_VALUE_AS_NUMBER(instructions[offset++]) << "'";
        break;

    case OpCode::CALL: os << " '" << instructions[offset++] << "'"; break;

    default: offset += SRCLANG_OPCODE_SIZE[static_cast<int>(op)]; break;
    }

    return offset;
}

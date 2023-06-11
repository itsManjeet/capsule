#include "ByteCode.hxx"
#include "SymbolTable.hxx"
#include "Utilities.hxx"

using namespace srclang;

int ByteCode::debug(Instructions const &instructions,
                    std::vector<Value> const &constants, int offset,
                    std::ostream &os) {
    os << std::setfill('0') << std::setw(4) << offset << " ";
    auto op = static_cast<OpCode>(instructions[offset]);
    os << SRCLANG_OPCODE_ID[static_cast<int>(op)];
    offset += 1;
    switch (op) {
        case OpCode::CONST: {
            auto pos = instructions[offset++];
            if (!constants.empty()) {
                os << " " << pos << " '"
                   << SRCLANG_VALUE_DEBUG(constants[pos]) << "'";
            }

        }
            break;
        case OpCode::INDEX:
        case OpCode::PACK:
        case OpCode::MAP:
        case OpCode::SET_SELF: {
            os << " " << (int) instructions[offset++];
        }
            break;
        case OpCode::CONTINUE:
        case OpCode::BREAK:
        case OpCode::JNZ:
        case OpCode::JMP: {
            auto pos = instructions[offset++];
            os << " '" << pos << "'";
        }
            break;
        case OpCode::LOAD:
        case OpCode::STORE: {
            auto scope = instructions[offset++];
            auto pos = instructions[offset++];
            os << " " << pos << " '" << SRCLANG_SYMBOL_ID[scope] << "'";
        }
            break;
        case OpCode::CLOSURE: {
            auto constantIndex = instructions[offset++];
            auto nfree = instructions[offset++];
            os << constants[constantIndex] << " " << nfree;
        }
            break;

        case OpCode::CONST_INT:
        case OpCode::CALL: {
            auto count = instructions[offset++];
            os << " '" << count << "'";
        }
            break;
        default:
            offset += SRCLANG_OPCODE_SIZE[int(op)];
            break;
    }

    return offset;
}

void ByteCode::dump(std::ostream &os) {
    dump_int<size_t>(instructions->size(), os);
    for (auto byte: *instructions) {
        dump_int<Byte>(byte, os);
    }
    dump_int<size_t>(constants.size(), os);
    for (auto i: constants) {
        SRCLANG_VALUE_DUMP(i, os);
    }
}


ByteCode *ByteCode::read(std::istream &is) {
    auto size = read_int<size_t>(is);
    auto bytecode = new ByteCode();
    bytecode->instructions = std::make_unique<Instructions>();
    for (auto i = 0; i < size; i++) {
        bytecode->instructions->push_back(read_int<Byte>(is));
    }
    size = read_int<size_t>(is);
    for (auto i = 0; i < size; i++) {
        bytecode->constants.push_back(SRCLANG_VALUE_READ(is));
    }
    return bytecode;
}


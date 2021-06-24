#ifndef __SRC_CODE__
#define __SRC_CODE__

#include <vector>
#include <ostream>
#include <string>

namespace src::lang
{
#define src_code 0x12345

    enum bytecode
    {
        NEG,
        ADD,
        SUB,
        MUL,
        DIV,

        NOT,
        EQ,
        NE,
        LT,
        LE,
        GT,
        GE,

        AND,
        OR,

        LOAD,
        STORE,

        LOAD_MEM,
        STORE_MEM,
        
        INT,

        JMP_IF,
        JMP,

        ADJ_STK,
        CALL,
        RET
    };

    inline std::string bytecode_to_str(bytecode c)
    {
        switch (c)
        {
        case NEG:
            return "neg";
        case ADD:
            return "add";
        case SUB:
            return "sub";
        case MUL:
            return "mul";
        case DIV:
            return "div";
        case NOT:
            return "not";
        case EQ:
            return "eq";
        case NE:
            return "ne";
        case LT:
            return "lt";
        case LE:
            return "le";
        case GT:
            return "gt";
        case GE:
            return "ge";

        case AND:
            return "and";
        case OR:
            return "or";
        case LOAD:
            return "load";
        case STORE:
            return "store";
        case LOAD_MEM:
            return "load_mem";
        case STORE_MEM:
            return "store_mem";

        case INT:
            return "int";
        case JMP_IF:
            return "jmpif";
        case JMP:
            return "jmp";
        case ADJ_STK:
            return "adjstk";

        case CALL:
            return "call";
        case RET:
            return "ret";
        default:
            return std::to_string(c);
        }
    }
}

#endif
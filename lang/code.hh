#ifndef __SRC_CODE__
#define __SRC_CODE__

#include <vector>

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

        INT,

        JMP_IF,
        JMP,

        ADJ_STK,
        CALL,
        RET
    };
}

#endif
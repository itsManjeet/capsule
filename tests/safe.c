#include <stdio.h>
#include <stdlib.h>

int _src_unary_not(int val)
{
    return !val;
}

int64_t _src_unary_neg(u_int64_t val)
{
    return -val;
}

void** _src_unary_ptr(void* val)
{
    return &val;
}

#ifndef __SRC_INTERPRETER__
#define __SRC_INTERPRETER__

#include "../lang/code.hh"

namespace src::interpreter
{
    using namespace src::lang;

    class machine
    {
    private:
        std::vector<int> _stack;
        int execute(
            std::vector<int> const &code,
            std::vector<int>::const_iterator pc,
            std::vector<int>::iterator frame_ptr)
        {
            auto stack_ptr = frame_ptr;
            while (true)
            {
                switch (*pc++)
                {
                case NEG:
                    stack_ptr[-1] = -stack_ptr[-1];
                    break;

                case NOT:
                    stack_ptr[-1] = !bool(stack_ptr[-1]);
                    break;

                case ADD:
                    --stack_ptr;
                    stack_ptr[-1] += stack_ptr[0];
                    break;

                case SUB:
                    --stack_ptr;
                    stack_ptr[-1] -= stack_ptr[0];
                    break;

                case MUL:
                    --stack_ptr;
                    stack_ptr[-1] *= stack_ptr[0];
                    break;

                case DIV:
                    --stack_ptr;
                    stack_ptr[-1] = static_cast<int>(stack_ptr[-1] / stack_ptr[0]);
                    break;

                case EQ:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] == stack_ptr[0]);
                    break;

                case NE:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] != stack_ptr[0]);
                    break;

                case LT:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] < stack_ptr[0]);
                    break;

                case LE:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] <= stack_ptr[0]);
                    break;

                case GT:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] > stack_ptr[0]);
                    break;

                case GE:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] >= stack_ptr[0]);
                    break;

                case AND:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] && stack_ptr[0]);
                    break;

                case OR:
                    --stack_ptr;
                    stack_ptr[-1] = bool(stack_ptr[-1] || stack_ptr[0]);
                    break;

                case LOAD:
                    *stack_ptr++ = frame_ptr[*pc++];
                    break;

                case STORE:
                    --stack_ptr;
                    frame_ptr[*pc++] = stack_ptr[0];
                    break;

                case INT:
                    *stack_ptr++ = *pc++;
                    break;

                case JMP:
                    pc += *pc;
                    break;

                case JMP_IF:
                    if (!bool(stack_ptr[-1]))
                        pc += *pc;
                    else
                        ++pc;
                    --stack_ptr;
                    break;

                case ADJ_STK:
                    stack_ptr += *pc++;
                    break;

                case CALL:
                {
                    int nargs = *pc++;
                    int jump = *pc++;

                    int r = execute(
                        code,
                        code.begin() + jump,
                        stack_ptr - nargs);

                    stack_ptr[-nargs] = r;
                    stack_ptr -= (nargs - 1);
                }
                break;

                case RET:
                    return stack_ptr[-1];

                default:
                    io::error("unsupported bytecode: ", std::to_string(*(pc-1)));
                    return 1;
                }
            }
        }

    public:
        machine(unsigned size = 4096)
            : _stack(size)
        {
        }

        int execute(std::vector<int> const &code)
        {
            return execute(code, code.begin(), _stack.begin());
        }

        std::vector<int> const &stack() const
        {
            return _stack;
        }

        std::vector<int> &stack()
        {
            return _stack;
        }
    };
}

#endif
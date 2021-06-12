#ifndef __LEXER__
#define __LEXER__

#include <string>

#include <utils/utils.hh>

namespace src::lang
{

    template <typename iterator>
    class lexer
    {
    private:
        std::string _ident;
        long _num;

        iterator _iter, _end;

    public:
        enum class type : int
        {
            eof,

            ident,
            number,
        };

        lexer(iterator start, iterator end)
            : _iter(start), _end(end)
        {
        }

        DEFINE_GET_METHOD(std::string, ident);
        DEFINE_GET_METHOD(long, num);

        int eat_token()
        {
            while (isspace(*_iter))
                _iter++;

            iterator pos = _iter;

            // identifier ::= [a-zA-Z][a-zA-Z0-9]*
            if (isalpha(*pos))
            {
                ++_iter;
                while (isalnum(*_iter))
                    ++_iter;

                _ident = std::string(pos, _iter);
                return type::ident;
            }

            // number ::= [0-9]+
            if (isdigit(*pos))
            {
                do
                {
                    ++_iter;
                } while (isdigit(*_iter));

                _num = std::stol(std::string(pos, _iter));
                return type::number;
            }

            if (pos == _end)
                return type::eof;

            ++_iter;

            return *pos;
        }
    };
}

#endif
#ifndef __LEXER__
#define __LEXER__

#include <string>
#include <rlx.hh>
#include <utils/utils.hh>

namespace src::lang
{
    namespace io = rlx::io;
    using color = rlx::io::color;
    using level = rlx::io::debug_level;

    enum tokentype
    {
        eof = -1,

        if_ = 5,
        else_,
        for_,
        while_,
        ret,

        to,
        vari,

        fn,
        let,
        use,
        ptr,
        ref,
        struct_,
        and_,
        or_,
        not_,

        eq,
        ne,
        le,
        ge,

        ident,
        str,
        number,
    };

    inline std::string tokentostr(int t)
    {
        switch (t)
        {
        case eof:
            return "EOF";
        case if_:
            return "if";
        case else_:
            return "else";
        case for_:
            return "for";

        case while_:
            return "while";

        case ret:
            return "ret";
        case fn:
            return "fn";
        case let:
            return "let";

        case ptr:
            return "ptr";
        case ref:
            return "ref";

        case struct_:
            return "struct";

        case and_:
            return "and";
        case or_:
            return "or";
        case not_:
            return "not";

        case ident:
            return "ident";
        case str:
            return "str";
        case number:
            return "num";
        default:
            return std::string(1, (char)t);
        }
    }
    template <typename iterator>
    class lexer
    {
    private:
        std::string _ident;
        long _num;

        iterator _iter, _start, _end;

    public:
        lexer(iterator start, iterator end)
            : _iter(start), _start(start), _end(end)
        {
        }

        DEFINE_GET_METHOD(std::string, ident);
        DEFINE_GET_METHOD(long, num);

        void throw_error(std::string mesg, iterator err_pos) const
        {
            int line;
            iterator line_start = get_pos(err_pos, line);
            if (err_pos != _end)
            {
                io::println(get_line(line_start));

                for (; line_start != err_pos; ++line_start)
                    io::print(' ');

                io::println("^");
                io::error(mesg, color::CYAN, " : ", color::RESET, color::BOLD, color::MAGENTA, " line ", color::RESET, color::BOLD, color::BLUE, line);
            }
            else
            {
                io::info("unexpected end of file");
                io::error(mesg, color::CYAN, " : ", color::RESET, color::BOLD, color::MAGENTA, " line ", color::RESET, color::BOLD, color::BLUE, line);
            }

            throw std::runtime_error("failed");
        }

        iterator get_pos(iterator err_pos, int &line) const
        {
            line = 1;
            iterator i = _start;
            iterator line_start = _start;

            while (i != err_pos)
            {
                bool eol = false;
                if (i != err_pos && *i == '\r')
                {
                    eol = true;
                    line_start = ++i;
                }

                if (i != err_pos && *i == '\n')
                {
                    eol = true;
                    line_start = ++i;
                }

                if (eol)
                    ++line;
                else
                    ++i;
            }

            return line_start;
        }

        std::string get_line(iterator err_pos) const
        {
            iterator i = err_pos;
            while (i != _end && (*i != '\r' && *i != '\n'))
                ++i;

            return std::string(err_pos, i);
        }

        void throw_error(std::string mesg)
        {
            return throw_error(mesg, _iter);
        }

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
                if (_ident == "if")
                    return tokentype::if_;
                if (_ident == "else")
                    return tokentype::else_;
                if (_ident == "for")
                    return tokentype::for_;
                if (_ident == "while")
                    return tokentype::while_;
                if (_ident == "ret")
                    return tokentype::ret;
                if (_ident == "fn")
                    return tokentype::fn;
                if (_ident == "let")
                    return tokentype::let;
                if (_ident == "ptr")
                    return tokentype::ptr;
                if (_ident == "ref")
                    return tokentype::ref;
                if (_ident == "and")
                    return tokentype::and_;
                if (_ident == "or")
                    return tokentype::or_;
                if (_ident == "not")
                    return tokentype::not_;
                if (_ident == "use")
                    return tokentype::use;
                if (_ident == "struct")
                    return tokentype::struct_;

                return tokentype::ident;
            }

            // number ::= [0-9]+
            if (isdigit(*pos))
            {
                do
                {
                    ++_iter;
                } while (isdigit(*_iter));

                _num = std::stol(std::string(pos, _iter));
                return tokentype::number;
            }

            // str ::= '"' .*? '"'
            if (*pos == '"')
            {
                std::string id;
                do
                {
                    ++_iter;
                    if (*_iter == '\\')
                    {
                        io::debug(level::debug, "found \\ in string with ", *(_iter + 1));
                        switch (*(++_iter))
                        {
                        case 'n':
                            id += '\n';
                            break;
                        case 'r':
                            id += '\r';
                            break;
                        case '"':
                            id += '\"';
                            break;
                        case '\'':
                            id += '\'';
                            break;
                        case '\\':
                            id += '\\';
                            break;
                        default:
                            id += "\\" + *(_iter);
                        }
                        _iter++;
                    }
                    else if (*_iter == '"')
                        break;
                    else
                        id += *_iter;

                } while (*_iter != '"');
                _iter++;

                _ident = id.substr(0, id.length());
                io::debug(level::trace, "got string ", _ident);
                return tokentype::str;
            }

            auto symbol_match = [&](std::string const &x) -> bool
            {
                int len = x.length();
                auto symbol = std::string(pos, pos + len);
                if (symbol == x)
                {
                    io::debug(level::debug, "found symbol ", symbol);
                    _iter += len;
                    return true;
                }
                return false;
            };

            if (symbol_match("=="))
                return tokentype::eq;

            if (symbol_match("!="))
                return tokentype::ne;

            if (symbol_match(">="))
                return tokentype::ge;

            if (symbol_match("<="))
                return tokentype::le;

            if (symbol_match("->"))
                return tokentype::to;

            if (symbol_match("..."))
                return tokentype::vari;

            if (pos == _end)
            {
                _iter++;
                return tokentype::eof;
            }

            ++_iter;

            return *pos;
        }
    };
}

#endif
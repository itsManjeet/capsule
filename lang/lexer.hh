#ifndef __LEXER__
#define __LEXER__

#include <string>
#include <map>
#include <rlx.hh>
#include <utils/utils.hh>

namespace src::lang
{
    namespace io = rlx::io;

    using std::string;
    using color = rlx::io::color;
    using level = rlx::io::debug_level;

    enum class token : char
    {
        _EOF,

        IF,
        ELSE,
        FOR,
        WHILE,
        RET,

        TO,
        VARI,

        FN,
        LET,
        USE,
        PTR,
        REF,
        STRUCT,
        AND,
        OR,
        NOT,

        PLUS,
        MINUS,
        MUL,
        DIV,
        DOT,
        EQUAL,

        LPAREN,
        RPAREN,
        LBRACK,
        RBRACK,
        LBRACE,
        RBRACE,

        EQ,
        NE,
        LT,
        LE,
        GT,
        GE,

        IDENT,
        STR,
        NUM,

        COLON,
        SEMICOLON,
        COMMA,

    };

    template <typename E>
    constexpr typename std::underlying_type<E>::type _T(E e) noexcept
    {
        return static_cast<typename std::underlying_type<E>::type>(e);
    }

    template <typename iterator>
    class lexer
    {

    public:
        class exception : public std::exception
        {
            string _what;

        public:
            exception(string w)
                : _what(w)
            {
            }

            const char *what()
            {
                return _what.c_str();
            }
        };

    private:
        std::string _ident;
        long _num;

        iterator _iter, _start, _end;

        std::map<string, token> keywords = {
            {"if", token::IF},
            {"else", token::ELSE},
            {"for", token::FOR},
            {"while", token::WHILE},
            {"ret", token::RET},
            {"fn", token::FN},
            {"let", token::LET},
            {"use", token::USE},
            {"ptr", token::PTR},
            {"ref", token::REF},
            {"struct", token::STRUCT},
            {"and", token::AND},
            {"or", token::OR},
            {"not", token::NOT},
        };

        std::map<string, token> symbols = {
            {"==", token::EQ},
            {"!=", token::NE},
            {">=", token::GE},
            {"<=", token::LE},
            {"->", token::TO},
            {"...", token::VARI},
        };

        std::map<char, token> knowns = {
            {'+', token::PLUS},
            {'-', token::MINUS},
            {'*', token::MUL},
            {'/', token::DIV},
            {'.', token::DOT},
            {'=', token::EQUAL},

            {'(', token::LPAREN},
            {')', token::RPAREN},
            {'[', token::LBRACK},
            {']', token::RBRACK},
            {'{', token::LBRACE},
            {'}', token::RBRACE},
            {'<', token::LT},
            {'>', token::GT},
            {':', token::COLON},
            {';', token::SEMICOLON},
            {',', token::COMMA},
        };

    public:
        lexer(iterator start, iterator end) : _iter(start),
                                              _start(start),
                                              _end(end)
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

                for (; line_start != err_pos - 1; ++line_start)
                    io::print(' ');

                io::println("^");
            }
            else
                io::info("unexpected end of file");

            throw lexer::exception(io::format(mesg, color::CYAN, " : ", color::RESET, color::BOLD, color::MAGENTA, " line ", color::RESET, color::BOLD, color::BLUE, line));
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

        string to_string(token t) const
        {
            for (auto const &i : keywords)
                if (i.second == t)
                    return i.first;
            for (auto const &i : symbols)
                if (i.second == t)
                    return i.first;
            for (auto const &i : knowns)
                if (i.second == t)
                    return string(1, i.first);

            return string(1, _T(t));
        }

        token eat_token()
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
                if (keywords.find(_ident) != keywords.end())
                    return keywords[_ident];

                return token::IDENT;
            }

            // number ::= [0-9]+
            if (isdigit(*pos))
            {
                do
                {
                    ++_iter;
                } while (isdigit(*_iter));

                _num = std::stol(std::string(pos, _iter));
                return token::NUM;
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
                        case 't':
                            id += '\r';
                            break;
                        case 'a':
                            id += '\a';
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
                return token::STR;
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

            for (auto const &i : symbols)
                if (symbol_match(i.first))
                    return i.second;

            if (pos == _end)
            {
                _iter++;
                return token::_EOF;
            }

            ++_iter;

            if (knowns.find(*pos) == knowns.end())
                throw_error("unknown symbol found");

            return knowns[*pos];
        }
    };
}

#endif
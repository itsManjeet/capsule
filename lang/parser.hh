#ifndef __PARSER__
#define __PARSER__

#include "ast.hh"
#include "lexer.hh"
#include <map>

namespace src::lang
{
    template <typename iterator>
    class parser
    {
    private:
        lexer<iterator> _lexer;
        token curtok, peektok;

        std::map<token, int> prec = {
            {token::OR, 2},
            {token::AND, 3},
            {token::NOT, 4},
            {token::EQUAL, 10},
            {token::EQ, 10},
            {token::NE, 10},
            {token::LT, 20},
            {token::LE, 20},
            {token::GT, 20},
            {token::GE, 20},
            {token::PLUS, 30},
            {token::MINUS, 30},
            {token::DIV, 40},
            {token::MUL, 40},
            {token::LPAREN, 50},
            {token::LBRACK, 50},
            {token::DOT, 50},
        };

        std::vector<token> infix = {
            token::LBRACK,
            token::LPAREN,
            token::DOT,
            token::EQUAL,
        };

        std::vector<token> operators = {
            token::PLUS,
            token::MINUS,
            token::DIV,
            token::MUL,
            token::LT,
            token::EQUAL,
            token::GT,
            token::EQ,
            token::NE,
            token::GE,
            token::LE,
            token::AND,
            token::OR,
        };

        int get_prec(token t)
        {
            int tp = prec[t];
            io::debug(level::trace, "found prec ", tp, " for ", _lexer.to_string(t));
            return tp <= 0 ? -1 : tp;
        }

        void eat()
        {
            io::debug(level::trace, "eating ", _lexer.to_string(curtok));
            curtok = _lexer.eat_token();
        }

        void check(token t)
        {
            if (curtok != t)
                _lexer.throw_error("expected '" + _lexer.to_string(t) + "' but got '" + _lexer.to_string(curtok) + "'");
        }

        void expect(token t)
        {
            check(t);
            eat();
        }

        /// ident ::= ident
        ast::ident parse_ident()
        {
            io::debug(level::trace, "parsing identifier");
            ast::ident ident;

            check(token::IDENT);
            ident.id = _lexer.ident();
            eat();

            return ident;
        }

        /// datatype ::= ident
        ast::datatype parse_type()
        {
            io::debug(level::trace, "parsing datatype");
            ast::datatype type_;

            // eat ident
            type_.value = parse_ident();

            return type_;
        }

        /// use ::= 'use' str ';'
        ast::use parse_use()
        {
            io::debug(level::trace, "parsing use");
            ast::use use;

            // eat 'use'
            eat();

            // eat str
            use.path = parse_str();

            // eat ';'
            expect(token::SEMICOLON);

            return use;
        }

        /// proto ::=  'fn' ident '(' -(args % ',') ',' '...' ')' '->' datatype
        ast::proto parse_proto()
        {
            io::debug(level::trace, "parsing proto type");

            ast::proto proto;

            // eat 'fn'
            expect(token::FN);

            // eat ident
            check(token::IDENT);
            proto.id = parse_ident();

            // eat '('
            expect(token::LPAREN);

            while (curtok != token::RPAREN)
            {
                if (curtok == token::VARI)
                {
                    proto.is_variadic = true;
                    eat();
                    break;
                }
                proto.args.push_back(parse_ident());
                if (curtok == token::RPAREN)
                    break;

                expect(token::COMMA);
            }

            // eat ')'
            expect(token::RPAREN);

            return proto;
        }

        /// fn ::= proto body
        ast::fn parse_fn(ast::proto p)
        {
            io::debug(level::trace, "parsing fn");
            ast::fn fn;
            fn.proto_ = p;
            fn.body = parse_block();

            return fn;
        }

        /// stmt ::= condition
        ///      ::= loop
        ///      ::= let
        ///      ::= expr_stmt
        ///      ::= ';'
        ast::stmt parse_stmt()
        {
            io::debug(level::trace, "parsing statement");
            switch (curtok)
            {
            case token::USE:
                return parse_use();

            case token::FN:
                return parse_fn(parse_proto());

            case token::LET:
                return parse_let();

            case token::IF:
                return parse_condition();

            case token::FOR:
                return parse_for_loop();

            case token::WHILE:
                return parse_while_loop();

            case token::BREAK:
                eat();
                eat();
                return ast::break_{};

            case token::CONTINUE:
                eat();
                eat();
                return ast::continue_{};

            case token::RET:
                return parse_ret();

            case token::LBRACE:
                return parse_block();

            case token::SEMICOLON:
                return ast::nil{};
            }

            return parse_expr_stmt();
        }

        /// block ::= '{' -(stmt) '}'
        ast::block parse_block()
        {
            io::debug(level::trace, "parsing block");
            ast::block block;
            // eat '{'
            eat();

            while (curtok != token::RBRACE)
                block.push_back(parse_stmt());

            // eat '}'
            expect(token::RBRACE);

            return block;
        }

        /// let ::= 'let' variable -('=' expr ';')
        ast::let parse_let()
        {
            io::debug(level::trace, "parsing let");
            ast::let let;

            // eat 'let'
            expect(token::LET);

            // eat variable
            let.var = parse_ident();

            if (curtok == token::EQUAL)
            {
                // eat '='
                expect(token::EQUAL);

                // eat expr
                let.val = parse_expr(-1);
            }
            else
            {
                let.val = ast::nil{};
            }

            // eat ';'
            expect(token::SEMICOLON);

            return let;
        }

        /// ret ::= 'ret' expr ';'
        ast::ret parse_ret()
        {
            io::debug(level::trace, "parsing return");
            ast::ret ret;
            // eat return
            eat();

            if (curtok != token::SEMICOLON)
                ret.val = parse_expr(-1);

            expect(token::SEMICOLON);

            return ret;
        }

        /// condition ::= 'if' expr stmt -('else' stmt)
        ast::condition parse_condition()
        {
            io::debug(level::trace, "parsing condition");
            ast::condition condition;
            // eat 'if'
            eat();

            // eat expr
            condition.cond = parse_expr(-1);

            // eat stmt
            condition.then_ = parse_stmt();

            if (curtok == token::ELSE)
            {
                // eat 'else'
                eat();

                // eat stmt
                condition.else_ = parse_stmt();
            }

            return condition;
        }

        /// for_loop ::= 'for' stmt expr stmt stmt
        ast::for_loop parse_for_loop()
        {
            io::debug(level::trace, "parsing for loop");
            ast::for_loop loop;

            // eat 'for'
            eat();

            // eat stmt
            loop.init = parse_stmt();

            // eat expr
            loop.cond = parse_expr(-1);

            // eat ';'
            eat();

            // eat stmt
            loop.inc = parse_stmt();

            // eat body
            loop.body = parse_stmt();

            return loop;
        }

        /// loop ::= 'while'  expr  stmt
        ast::while_loop parse_while_loop()
        {
            io::debug(level::trace, "parsing while loop");

            ast::while_loop loop;

            // eat 'while'
            eat();

            // eat expr
            loop.cond = parse_expr(-1);

            // eat stmt
            loop.body = parse_stmt();

            return loop;
        }

        ast::expr_stmt parse_expr_stmt()
        {
            io::debug(level::trace, "parsing expression statement");

            ast::expr_stmt expr_stmt;
            expr_stmt.expr_ = parse_expr(-1);

            expect(token::SEMICOLON);

            return expr_stmt;
        }

        /// expr ::= prefix -infix
        ast::expr parse_expr(int prec)
        {
            io::debug(level::trace, "parsing expr with prec ", prec);

            auto lhs = parse_prefix();

            while (curtok != token::SEMICOLON && prec < get_prec(curtok))
            {
                if (std::find(infix.begin(), infix.end(), curtok) == infix.end() &&
                    std::find(operators.begin(), operators.end(), curtok) == operators.end())
                    return lhs;

                // eat infix
                lhs = parse_infix(lhs);
            }

            return lhs;
        }

        /// prefix  ::= int
        ///         ::= str
        ///         ::= ident
        ast::expr parse_prefix()
        {
            io::debug(level::trace, "parsing prefix");
            switch (curtok)
            {
            case token::NUM:
                return parse_num();

            case token::STR:
                return parse_str();

            case token::IDENT:
                return parse_ident();

            case token::LBRACK:
                return parse_array();

            case token::PTR:
            case token::REF:
            case token::MINUS:
            case token::NOT:
                return parse_unary();
            }

            _lexer.throw_error("unexpected prefix '" + _lexer.to_string(curtok) + "'");
            return ast::nil{};
        }

        /// unary ::= <oper> expr
        ast::unary parse_unary()
        {
            ast::unary unary;

            // eat <oper>
            unary.oper = curtok;
            eat();

            // eat expr
            unary.lhs = parse_expr(get_prec(unary.oper));

            return unary;
        }

        unsigned int parse_num()
        {
            io::debug(level::trace, "parsing number");
            // eat 'num'
            unsigned int n = _lexer.num();
            eat();

            return n;
        }

        std::string parse_str()
        {
            io::debug(level::trace, "parsing string");
            // eat str
            std::string s = _lexer.ident();
            eat();

            return s;
        }

        /// array ::= '[' (expr  % ',' ) ']'
        ast::array parse_array()
        {
            io::debug(level::trace, "parsing array");

            ast::array array;
            // eat '['
            eat();

            while (curtok != token::RBRACK)
            {
                array.push_back(parse_expr(-1));
                if (curtok == token::RBRACK)
                    break;

                expect(token::COMMA);
            }

            // eat ']'
            expect(token::RBRACK);
            return array;
        }

        ast::expr parse_infix(ast::expr lhs)
        {
            io::debug(level::trace, "parsing infix");
            switch (curtok)
            {
            case token::LPAREN:
                return parse_call(lhs);
            case token::EQUAL:
                return parse_assign(lhs);
            case token::RBRACK:
                return parse_index(lhs);
            }

            token t = curtok;
            eat();

            io::debug(level::trace, "parsing binary");
            auto rhs = parse_expr(get_prec(t));
            return ast::binary{
                .oper = t,
                .lhs = lhs,
                .rhs = rhs};
        }

        /// call ::= lhs '(' -[expr % ','] ')'
        ast::call parse_call(ast::expr lhs)
        {
            io::debug(level::trace, "parsing call");
            ast::call call;

            call.id = lhs;
            // eat '('
            expect(token::LPAREN);

            while (curtok != token::RPAREN)
            {
                call.args.push_back(parse_expr(-1));
                if (curtok == token::RPAREN)
                    break;

                expect(token::COMMA);
            }

            expect(token::RPAREN);

            return call;
        }

        /// assign ::= lhs '=' rhs
        ast::assign parse_assign(ast::expr lhs)
        {
            io::debug(level::trace, "parsing assign");
            ast::assign assign;
            assign.id = lhs;

            // eat '='
            eat();

            // eat rhs
            assign.val = parse_expr(-1);

            return assign;
        }

        /// index ::= lhs '[' rhs ']'
        ast::index parse_index(ast::expr lhs)
        {
            io::debug(level::trace, "parsing index");
            ast::index index;
            index.id = lhs;

            // eat '['
            expect(token::LBRACK);

            index.val = parse_expr(-1);

            // eat ']'
            expect(token::RBRACK);

            return index;
        }

    public:
        parser(lexer<iterator> &l)
            : _lexer(l)
        {
            eat();
        }

        /// prog ::= *stmts
        ast::block parse()
        {
            ast::block decls;
            while (curtok != token::_EOF)
                decls.push_back(parse_stmt());

            return decls;
        }
    };
}
#endif
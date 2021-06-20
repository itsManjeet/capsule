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
        int curtok, peektok;

        std::map<int, int> prec = {
            {or_, 2},
            {and_, 3},
            {not_, 4},
            {'=', 10},
            {eq, 10},
            {ne, 10},
            {'<', 20},
            {le, 20},
            {'>', 20},
            {ge, 20},
            {'+', 30},
            {'-', 30},
            {'/', 40},
            {'*', 40},
            {'%', 40},
            {'(', 50},
            {'[', 50},
            {'.', 50},
        };

        bool is_infix(int c)
        {
            return c == '[' ||
                   c == '(' ||
                   c == '.' ||
                   c == '=';
        }

        bool is_oper(int c)
        {
            return c == '+' ||
                   c == '-' ||
                   c == '/' ||
                   c == '*' ||
                   c == '=' ||
                   c == '>' ||
                   c == '<' ||
                   c == eq ||
                   c == ne ||
                   c == le ||
                   c == ge ||
                   c == and_ ||
                   c == or_;
        }

        int get_prec(int t)
        {
            int tp = prec[t];
            io::debug(level::trace, "found prec ", tp, " for ", std::to_string(t), " | ", (char)(t));
            return tp <= 0 ? -1 : tp;
        }

        void eat()
        {
            io::debug(level::trace, "eating ", tokentostr(curtok), (curtok == ident ? ":" + _lexer.ident() : ""));
            curtok = _lexer.eat_token();
        }

        void check(int t)
        {
            if (curtok != t)
                _lexer.throw_error("expected '" + tokentostr(t) + "' but got '" + tokentostr(curtok) + "'");
        }

        void expect(int t)
        {
            check(t);
            eat();
        }

        /// ident ::= ident
        ast::ident parse_ident()
        {
            io::debug(level::trace, "parsing identifier");
            ast::ident ident;

            check(tokentype::ident);
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

        /// variable ::= ident ':' -(ptr|ref) datatype -('[' int ']')
        ast::variable parse_variable()
        {
            io::debug(level::trace, "parsing variable");

            ast::variable variable;
            // eat ident
            variable.ident_ = parse_ident();

            // eat ':'
            expect(':');

            while (curtok == tokentype::ref ||
                   curtok == tokentype::ptr)
            {
                variable.args.push_back(curtok);
                eat();
            }

            // eat datatype
            variable.type_ = parse_type();

            // eat -('[' int ']')
            if (curtok == '[')
            {
                // eat '['
                eat();

                // eat int
                variable.size = parse_num();

                // eat ']'
                expect(']');
            }

            return variable;
        }

        /// root_decl ::= proto ';'
        ///           ::= proto block
        ///           ::= 'use' str ';'
        ///           ::= 'struct' ident '{' (variables % ',') '}' ';'
        ast::root_decl parse_root_decl()
        {
            io::debug(level::trace, "parsing root decl");
            switch (curtok)
            {
            case tokentype::use:
                return parse_use();

            case tokentype::struct_:
                return parse_struct();

            case tokentype::fn:
            {
                // eat proto
                auto proto = parse_proto();

                // eat ';'
                if (curtok == ';')
                {
                    eat();
                    return proto;
                }

                return parse_fn(proto);
            }
            }

            _lexer.throw_error("unknown starting declaration");
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
            expect(';');

            return use;
        }

        /// struct ::= 'struct' ident '{' -(variable % ',') '}' ';'
        ast::struct_ parse_struct()
        {
            io::debug(level::trace, "parsing struct");
            ast::struct_ struct_;

            // eat 'struct'
            eat();

            // eat ident
            struct_.id = parse_ident();

            // eat '{'
            expect('{');

            // eat -(variable % ',')
            while (curtok != '}')
            {
                // eat variable
                struct_.vars.push_back(parse_variable());

                if (curtok == '}')
                    break;

                // eat ','
                expect(',');
            }

            // eat '}'
            expect('}');

            // eat ';'
            expect(';');

            return struct_;
        }

        /// proto ::=  'fn' ident '(' -(args % ',') ',' '...' ')' '->' datatype
        ast::proto parse_proto()
        {
            io::debug(level::trace, "parsing proto type");

            ast::proto proto;

            // eat 'fn'
            expect(tokentype::fn);

            // eat ident
            check(tokentype::ident);
            proto.id = parse_ident();

            // eat '('
            expect('(');

            while (curtok != ')')
            {
                if (curtok == tokentype::vari)
                {
                    proto.is_variadic = true;
                    eat();
                    break;
                }
                proto.args.push_back(parse_variable());
                if (curtok == ')')
                    break;

                expect(',');
            }

            // eat ')'
            expect(')');

            // eat '->'
            expect(tokentype::to);

            // eat type
            check(tokentype::ident);
            proto.type_ = parse_type();

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
            case let:
                return parse_let();

            case if_:
                return parse_condition();

            case for_:
                return parse_for_loop();

            case while_:
                return parse_while_loop();

            case ret:
                return parse_ret();

            case '{':
                return parse_block();

            case ';':
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

            while (curtok != '}')
                block.push_back(parse_stmt());

            // eat '}'
            expect('}');

            return block;
        }

        /// let ::= 'let' variable -('=' expr ';')
        ast::let parse_let()
        {
            io::debug(level::trace, "parsing let");
            ast::let let;

            // eat 'let'
            expect(tokentype::let);

            // eat variable
            let.var = parse_variable();

            if (curtok == '=')
            {
                // eat '='
                expect('=');

                // eat expr
                let.val = parse_expr(-1);
            }
            else
            {
                let.val = ast::nil{};
            }

            // eat ';'
            expect(';');

            return let;
        }

        /// ret ::= 'ret' expr ';'
        ast::ret parse_ret()
        {
            io::debug(level::trace, "parsing return");
            ast::ret ret;
            // eat return
            eat();

            if (curtok != ';')
                ret.val = parse_expr(-1);

            expect(';');

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

            if (curtok == tokentype::else_)
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

            expect(';');

            return expr_stmt;
        }

        /// expr ::= prefix -infix
        ast::expr parse_expr(int prec)
        {
            io::debug(level::trace, "parsing expr with prec ", prec);

            auto lhs = parse_prefix();

            while (curtok != ';' && prec < get_prec(curtok))
            {
                if (!is_infix(curtok) && !is_oper(curtok))
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
            case number:
                return parse_num();

            case str:
                return parse_str();

            case ident:
                return parse_ident();

            case '[':
                return parse_array();

            case ptr:
            case ref:
            case '-':
            case not_:
                return parse_unary();
            }

            _lexer.throw_error("unexpected prefix '" + std::string(1, (char)curtok) + "'");
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

            while (curtok != ']')
            {
                array.push_back(parse_expr(-1));
                if (curtok == ']')
                    break;

                expect(',');
            }

            // eat ']'
            expect(']');
            return array;
        }

        ast::expr parse_infix(ast::expr lhs)
        {
            io::debug(level::trace, "parsing infix");
            switch (curtok)
            {
            case '(':
                return parse_call(lhs);
            case '=':
                return parse_assign(lhs);
            case '[':
                return parse_index(lhs);

            default:
                int t = curtok;
                eat();

                auto rhs = parse_expr(get_prec(t));
                return ast::binary{
                    .oper = (char)t,
                    .lhs = lhs,
                    .rhs = rhs};
            }
        }

        /// call ::= lhs '(' -[expr % ','] ')'
        ast::call parse_call(ast::expr lhs)
        {
            io::debug(level::trace, "parsing call");
            ast::call call;

            call.id = lhs;
            // eat '('
            expect('(');

            while (curtok != ')')
            {
                call.args.push_back(parse_expr(-1));
                if (curtok == ')')
                    break;

                expect(',');
            }

            expect(')');

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
            expect('[');

            index.val = parse_expr(-1);

            // eat ']'
            expect(']');

            return index;
        }

    public:
        parser(lexer<iterator> &l)
            : _lexer(l)
        {
            eat();
        }

        /// prog ::= *root_decl
        ast::root_decls parse()
        {
            ast::root_decls decls;
            while (curtok != tokentype::eof)
                decls.push_back(parse_root_decl());

            return decls;
        }
    };
}
#endif
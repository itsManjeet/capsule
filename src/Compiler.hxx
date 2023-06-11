//
// Created by itsmanjeet on 11/6/23.
//

#ifndef SRCLANG_COMPILER_HXX
#define SRCLANG_COMPILER_HXX

#include <string>
#include <vector>
#include <iostream>

#include "Value.hxx"
#include "SymbolTable.hxx"
#include "MemoryManager.hxx"
#include "Instructions.hxx"
#include "ByteCode.hxx"
#include "Function.hxx"
#include "Options.hxx"


namespace srclang {
    using Iterator = std::string::const_iterator;

    class Language;

#define SRCLANG_TOKEN_TYPE_LIST \
    X(Reserved)                 \
    X(Character)                \
    X(Identifier)               \
    X(String)                   \
    X(Number)                   \
    X(Eof)

    enum class TokenType : uint8_t {
#define X(id) id,
        SRCLANG_TOKEN_TYPE_LIST
#undef X
    };

    static const std::vector<std::string> SRCLANG_TOKEN_ID = {
#define X(id) #id,
            SRCLANG_TOKEN_TYPE_LIST
#undef X
    };

    struct Token {
        TokenType type;
        std::string literal;
        Iterator pos;

        friend std::ostream &operator<<(std::ostream &os, const Token &token) {
            os << SRCLANG_TOKEN_ID[static_cast<int>(token.type)] << ":"
               << token.literal;
            return os;
        }
    };


    class Compiler {
    private:
        Language *language{nullptr};
        SymbolTable *symbol_table{nullptr};

        Token cur, peek;
        Iterator iter, start, end;
        std::string filename;

        std::vector<std::string> loaded_imports;
        std::vector<std::unique_ptr<Instructions >> instructions;
        DebugInfo *debug_info;
        int fileConst;
        std::shared_ptr<DebugInfo> global_debug_info;

        Instructions *inst();

        void push_scope();

        std::unique_ptr<Instructions> pop_scope();

        template<typename Message>
        void error(const Message &mesg, Iterator pos) const {
            int line;
            Iterator line_start = get_error_pos(pos, line);
            std::cout << filename << ":" << line << std::endl;
            if (pos != end) {
                std::cout << "ERROR: " << mesg << std::endl;
                std::cout << " | " << get_error_line(line_start) << std::endl << "   ";
                for (; line_start != pos; ++line_start) std::cout << ' ';
                std::cout << '^' << std::endl;
            } else {
                std::cout << "Unexpected end of file. ";
                std::cout << mesg << " line " << line << std::endl;
            }
        }

        Iterator get_error_pos(Iterator err_pos, int &line) const;

        [[nodiscard]] std::string get_error_line(Iterator err_pos) const;

        bool consume(const std::string &expected);

        bool consume(TokenType type);

        bool check(TokenType type);

        bool expect(const std::string &expected);

        bool expect(TokenType type);

        bool eat();

        enum Precedence {
            P_None = 0,
            P_Assignment,
            P_Or,
            P_And,
            P_Lor,
            P_Land,
            P_Equality,
            P_Comparison,
            P_Shift,
            P_Term,
            P_Factor,
            P_Unary,
            P_Call,
            P_Primary,
        };

        Precedence precedence(std::string tok);

        template<typename T, typename... Ts>
        int emit(T t, Ts... ts) {
            int line;
            get_error_pos(cur.pos, line);
            return inst()->emit(debug_info, line, t, ts...);
        }

        bool number();

        bool identifier(bool can_assign);

        bool string_();

        bool char_();

        bool unary(OpCode op);

        bool block();

        bool value(Symbol *symbol);

        /// fun '(' args ')' block
        bool function(Symbol *symbol, bool skip_args = false);


        /// list ::= '[' (<expression> % ',') ']'
        bool list();

        /// map ::= '{' ((<identifier> ':' <expression>) % ',') '}'
        bool map_();

        bool prefix(bool can_assign);

        bool assign();

        bool binary(OpCode op, int prec);

        /// call ::= '(' (expr % ',' ) ')'
        bool call();

        /// index ::= <expression> '[' <expession> (':' <expression>)? ']'
        bool index(bool can_assign);

        /// subscript ::= <expression> '.' <expression>
        bool subscript(bool can_assign);

        bool infix(bool can_assign);

        bool expression(int prec = P_Assignment);

        /// compiler_options ::= #![<option>(<value>)]
        bool compiler_options();

        /// let ::= 'let' 'global'? <identifier> '=' <expression>
        bool let();

        bool return_();

        void patch_loop(int loop_start, OpCode to_patch, int pos);

        /// loop ::= 'for' <expression> <block>
        /// loop ::= 'for' <identifier> 'in' <expression> <block>
        bool loop();

        /// import ::= 'import' <string> ('as' <identifier>)?
        bool import_();

        /// defer ::= 'defer' <function>
        bool defer();

        /// condition ::= 'if' <expression> <block> (else statement)?
        bool condition();

        ValueType type(std::string literal);

        /// native ::= 'native' <identifier> ( (<type> % ',') ) <type>
        bool native(Symbol *symbol);

        /// statement ::= set
        ///           ::= let
        ///           ::= return
        ///           ::= ';'
        ///           ::= expression ';'
        bool statement();

        bool program();

    public:

        Compiler(Iterator start, Iterator end, const std::string &filename, Language *language);

        bool compile();

        ByteCode code();

        std::shared_ptr<DebugInfo> debugInfo() { return global_debug_info; }

    };


} // srclang

#endif //SRCLANG_COMPILER_HXX

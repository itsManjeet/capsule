#ifndef SRCLANG_COMPILER_H
#define SRCLANG_COMPILER_H

#include "ByteCode.h"
#include "Function.h"
#include "Instructions.h"
#include "MemoryManager.h"
#include "SymbolTable.h"
#include "Value.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace SrcLang {
using Iterator = std::wstring::const_iterator;

#define SRCLANG_TOKEN_TYPE_LIST                                                \
    X(Reserved)                                                                \
    X(Identifier)                                                              \
    X(String)                                                                  \
    X(Number)                                                                  \
    X(Eof)

enum class TokenType : uint8_t {
#define X(id) id,
    SRCLANG_TOKEN_TYPE_LIST
#undef X
};

static const std::vector<std::wstring> SRCLANG_TOKEN_ID = {
        L"Reserved",
        L"Identifier",
        L"String",
        L"Number",
        L"Eof",
};

struct Token {
    TokenType type;
    std::wstring literal;
    Iterator pos;

    friend std::wostream& operator<<(std::wostream& os, const Token& token) {
        os << SRCLANG_TOKEN_ID[static_cast<int>(token.type)] << ":"
           << token.literal;
        return os;
    }
};

class Interpreter;

class Compiler {
private:
    SymbolTable* symbol_table;
    SymbolTable* global;
    Interpreter* interpreter;

    Token cur, peek;
    Iterator iter, start, end;
    std::wstring filename;

    std::vector<std::string> loaded_imports;
    std::vector<std::unique_ptr<Instructions>> instructions;
    DebugInfo* debug_info;
    std::shared_ptr<DebugInfo> global_debug_info;

    Instructions* inst();

    void push_scope();

    std::unique_ptr<Instructions> pop_scope();

    void error(const std::wstring& mesg, Iterator pos) {
        int line;
        Iterator line_start = get_error_pos(pos, line);
        std::wstringstream err;
        err << filename << L":" << line << L'\n';
        if (pos != end) {
            err << L"ERROR: " << mesg << L'\n';
            err << L" | " << get_error_line(line_start) << L'\n' << L"   ";
            for (; line_start != pos; ++line_start) err << L' ';
            err << L'^';
        } else {
            err << L"Unexpected end of file. ";
            err << mesg << L" line " << line;
        }
        throw std::runtime_error(ws2s(err.str()));
    }

    Iterator get_error_pos(Iterator err_pos, int& line) const;

    [[nodiscard]] std::wstring get_error_line(Iterator err_pos) const;

    bool consume(const std::wstring& expected);

    bool consume(TokenType type);

    void check(TokenType type);

    void expect(const std::wstring& expected);

    void expect(TokenType type);

    void eat();

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

    Precedence precedence(std::wstring tok);

    template <typename T, typename... Ts> int emit(T t, Ts... ts) {
        int line;
        get_error_pos(cur.pos, line);
        return inst()->emit(debug_info, line, t, ts...);
    }

    /// comment ::= '//' (.*) '\n'

    /// number ::= [0-9_.]+[bh]
    void number();

    /// identifier ::= [a-zA-Z_]([a-zA-Z0-9_]*)
    void identifier(bool can_assign);

    /// string ::= '"' ... '"'
    void string_();

    /// unary ::= ('+' | '-' | 'not') <expression>
    void unary(OpCode op);

    /// block ::= '{' <stmt>* '}'
    void block();

    void value(Symbol* symbol);

    /// fun '(' args ')' block
    void function(Symbol* symbol, bool skip_args = false);

    void native(Symbol* symbol);

    /// list ::= '[' (<expression> % ',') ']'
    void list();

    /// map ::= '{' ((<identifier> ':' <expression>) % ',') '}'
    void map_();

    /// prefix ::= number
    ///        ::= string
    ///        ::= identifier
    ///        ::= unary
    ///        ::= list
    ///        ::= map
    ///        ::= function
    ///        ::= '(' expression ')'
    void prefix(bool can_assign);

    /// binary ::= expr ('+' | '-' | '*' | '/' | '==' | '!=' | '<' | '>' | '>='
    /// | '<=' | 'and' | 'or' | '|' | '&' | '>>' | '<<' | '%') expr
    void binary(OpCode op, int prec);

    /// call ::= '(' (expr % ',' ) ')'
    void call();
    void call2();

    /// index ::= <expression> '[' <expession> (':' <expression>)? ']'
    void index(bool can_assign);

    /// subscript ::= <expression> '.' <expression>
    void subscript(bool can_assign);

    /// infix ::= call
    ///       ::= subscript
    ///       ::= index
    ///       ::= binary
    void infix(bool can_assign);

    /// expression ::= prefix infix*
    void expression(int prec = P_Assignment);

    /// compiler_options ::= #![<option>(<value>)]
    void compiler_options();

    /// let ::= 'let' 'global'? <identifier> '=' <expression>
    void let();

    /// return ::= 'return' <expression>
    void return_();

    void patch_loop(int loop_start, OpCode to_patch, int pos);

    /// loop ::= 'for' <expression> <block>
    /// loop ::= 'for' <identifier> 'in' <expression> <block>
    void loop();

    /// import ::= 'import' <string>
    void import_();

    /// defer ::= 'defer' <function>
    void defer();

    /// condition ::= 'if' <expression> <block> (else statement)?
    void condition();

    /// type ::= 'identifier'
    ValueType type(std::string literal);

    /// statement ::= let
    ///           ::= return
    ///           ::= ';'
    ///           ::= condition
    ///           ::= loop
    ///           ::= break
    ///           ::= continue
    ///           ::= defer
    ///           ::= compiler_options
    ///           ::= expression ';'
    void statement();

    /// program ::= statement*
    void program();

public:
    Compiler(const std::wstring& source, const std::wstring& filename,
            Interpreter* interpreter, SymbolTable* symbol_table);

    void compile();

    ByteCode code();

    std::shared_ptr<DebugInfo> debugInfo() { return global_debug_info; }
};

} // namespace SrcLang

#endif // SRCLANG_COMPILER_H

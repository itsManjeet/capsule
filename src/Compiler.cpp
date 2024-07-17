#include "Compiler.h"

#include "Interpreter.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <utility>

using namespace SrcLang;

template <class V> std::type_info const& variant_typeid(V const& v) {
    return visit([](auto&& x) -> decltype(auto) { return typeid(x); }, v);
}

ByteCode Compiler::code() {
    return ByteCode{std::move(instructions.back()), interpreter->constants};
}

Instructions* Compiler::inst() { return instructions.back().get(); }

void Compiler::push_scope() {
    symbol_table = new SymbolTable(symbol_table);
    instructions.push_back(std::make_unique<Instructions>());
}

std::unique_ptr<Instructions> Compiler::pop_scope() {
    auto old = symbol_table;
    symbol_table = symbol_table->parent;
    delete old;
    auto res = std::move(instructions.back());
    instructions.pop_back();
    return std::move(res);
}

Iterator Compiler::get_error_pos(Iterator err_pos, int& line) const {
    line = 1;
    Iterator i = start;
    if (i == end) return start;
    Iterator line_start = start;
    while (i != err_pos) {
        bool eol = false;
        if (i != err_pos && *i == '\r') // CR
        {
            eol = true;
            line_start = ++i;
        }
        if (i != err_pos && *i == '\n') // LF
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

std::wstring Compiler::get_error_line(Iterator err_pos) const {
    Iterator i = err_pos;
    // position i to the next EOL
    while (i != end && (*i != '\r' && *i != '\n')) ++i;
    return std::wstring(err_pos, i);
}

bool Compiler::consume(const std::wstring& expected) {
    if (cur.type != TokenType::Reserved ||
            expected.length() != cur.literal.length() ||
            !equal(expected.begin(), expected.end(), cur.literal.begin(),
                    cur.literal.end())) {
        return false;
    }
    eat();
    return true;
}

bool Compiler::consume(TokenType type) {
    if (cur.type != type) { return false; }
    eat();
    return true;
}

void Compiler::check(TokenType type) {
    if (cur.type != type) {
        std::wstringstream ss;
        ss << "Expected '" << SRCLANG_TOKEN_ID[static_cast<int>(type)]
           << "' but got '" << cur.literal << "'";
        error(ss.str(), cur.pos);
    }
}

void Compiler::expect(const std::wstring& expected) {
    if (cur.literal != expected) {
        std::wstringstream ss;
        ss << "Expected '" << expected << "' but got '" << cur.literal << "'";
        error(ss.str(), cur.pos);
    }
    return eat();
}

void Compiler::expect(TokenType type) {
    if (cur.type != type) {
        std::wstringstream ss;
        ss << "Expected '" << SRCLANG_TOKEN_ID[int(type)] << "' but got '"
           << cur.literal << "'";
        error(ss.str(), cur.pos);
    }
    return eat();
}

void Compiler::eat() {
    cur = peek;

    do {
        if (iter == end) {
            peek.type = TokenType::Eof;
            return;
        }
        if (!isspace(*iter)) break;
        ++iter;
    } while (true);

    peek.pos = iter;

    auto escape = [&](Iterator& iterator, bool& status) -> wchar_t {
        if (*iterator == L'\\') {
            auto const ech = *iterator++;
            ++iterator;
            switch (ech) {
            case L'a': return L'\a';
            case L'b': return L'\b';
            case L'n': return L'\n';
            case L't': return L'\t';
            case L'r': return L'\r';
            case L'\\': return L'\\';
            case L'\'': return L'\'';
            case L'"': return L'"';
            case L'0':
                if (*iter == L'3' && *(iter + 1) == L'3') {
                    iter += 2;
                    return L'\033';
                }
            default: error(L"invalid escape sequence", iterator - 1);
            }
        }
        return *iter++;
    };

    if (*iter == L'/' && *(iter + 1) == L'/') {
        iter += 2;
        while (*iter != '\n') {
            if (iter == end) {
                peek.type = TokenType::Eof;
                return;
            }
            ++iter;
        }
        ++iter;
        return eat();
    }

    if (*iter == L'"' || *iter == L'\'') {
        auto starting = *iter++;
        peek.literal.clear();
        bool status = true;
        while (*iter != starting) {
            if (iter == end) { error(L"unterminated string", peek.pos); }
            peek.literal += escape(iter, status);
        }
        ++iter;
        peek.type = TokenType::String;
        return;
    }

    for (std::wstring k : {L"let", L"fun", L"native", L"return", L"class",
                 L"if", L"else", L"for", L"break", L"continue", L"import",
                 L"global", L"as", L"in", L"defer",

                 // special operators
                 L"#!", L"not", L"...", L":=",

                 // multi char operators
                 L"==", L"!=", L"<=", L">=", L">>", L"<<"}) {
        auto dist = distance(k.begin(), k.end());
        if (dist < distance(iter, end) &&
                equal(iter, iter + dist, k.begin(), k.end()) &&
                !isalnum(*(iter + dist))) {
            iter += dist;
            peek.literal = std::wstring(k.begin(), k.end());
            peek.type = TokenType::Reserved;
            return;
        }
    }

    if (isalpha(*iter) || *iter == '_') {
        do { ++iter; } while (isalnum(*iter) || *iter == '_');
        peek.literal = std::wstring(peek.pos, iter);
        peek.type = TokenType::Identifier;
        return;
    }

    if (ispunct(*iter)) {
        peek.literal = std::wstring(peek.pos, ++iter);
        peek.type = TokenType::Reserved;
        return;
    }

    if (isdigit(*iter)) {
        do { ++iter; } while (isdigit(*iter) || *iter == '.' || *iter == '_');
        if (*iter == 'b' ||
                *iter ==
                        'h') { // include 'b' for binary and 'h' for hexadecimal
            ++iter;
        }
        peek.literal = std::wstring(peek.pos, iter);
        peek.type = TokenType::Number;
        return;
    }
    error(L"unexpected token", iter);
}

Compiler::Precedence Compiler::precedence(std::wstring tok) {
    static std::map<std::wstring, Precedence> prec = {
            {L":=", P_Assignment},
            {L"=", P_Assignment},
            {L"or", P_Or},
            {L"and", P_And},
            {L"&", P_Land},
            {L"|", P_Lor},
            {L"==", P_Equality},
            {L"!=", P_Equality},
            {L">", P_Comparison},
            {L"<", P_Comparison},
            {L">=", P_Comparison},
            {L"<=", P_Comparison},
            {L">>", P_Shift},
            {L"<<", P_Shift},
            {L"+", P_Term},
            {L"-", P_Term},
            {L"*", P_Factor},
            {L"/", P_Factor},
            {L"%", P_Factor},
            {L"not", P_Unary},
            {L"-", P_Unary},
            {L".", P_Call},
            {L"[", P_Call},
            {L"(", P_Call},
            {L"{", P_Call},
    };
    auto i = prec.find(tok);
    if (i == prec.end()) { return P_None; }
    return i->second;
}

void Compiler::number() {
    bool is_float = false;
    int base = 10;
    std::string number_value;
    if (cur.literal.length() > 1 && cur.literal[0] == '0') {
        base = 8;
        cur.literal = cur.literal.substr(1);
    }
    for (auto i : cur.literal) {
        if (i == '.') {
            if (is_float) {
                error(L"multiple floating point detected", cur.pos);
            }
            number_value += '.';
            is_float = true;
        } else if (i == '_') {
            continue;
        } else if (i == 'b') {
            base = 2;
        } else if (i == 'h') {
            base = 16;
        } else if (i == 'o') {
            base = 8;
        } else {
            number_value += i;
        }
    }
    try {
        if (is_float) {
            emit(OpCode::CONST_, SRCLANG_VALUE_NUMBER(stod(number_value)));
        } else {
            emit(OpCode::CONST_INT, SRCLANG_VALUE_NUMBER(std::stol(
                                            number_value, nullptr, base)));
        }
    } catch (std::invalid_argument const& e) {
        error(L"Invalid numerical value " + s2ws(e.what()), cur.pos);
    }

    return expect(TokenType::Number);
}

void Compiler::identifier(bool can_assign) {
    auto iter = std::find(SRCLANG_VALUE_TYPE_ID.begin(),
            SRCLANG_VALUE_TYPE_ID.end(), ws2s(cur.literal));
    if (iter != SRCLANG_VALUE_TYPE_ID.end()) {
        emit(OpCode::LOAD, Symbol::Scope::TYPE,
                distance(SRCLANG_VALUE_TYPE_ID.begin(), iter));
        return eat();
    }
    check(TokenType::Identifier);
    auto id = cur;
    eat();
    auto symbol = symbol_table->resolve(id.literal);

    if (can_assign && consume(L":=")) {
        if (symbol == std::nullopt) {
            symbol = symbol_table->define(id.literal);
        }
        value(&*symbol);
        emit(OpCode::STORE, symbol->scope, symbol->index);
    } else {
        if (symbol == std::nullopt) {
            error(L"undefined variable '" + id.literal + L"'", id.pos);
        }
        if (can_assign && consume(L"=")) {
            value(&*symbol);
            emit(OpCode::STORE, symbol->scope, symbol->index);
        } else {
            emit(OpCode::LOAD, symbol->scope, symbol->index);
        }
    }
}

void Compiler::string_() {
    auto string_value = SRCLANG_VALUE_STRING(wchdup(cur.literal.c_str()));
    interpreter->memoryManager.heap.push_back(string_value);
    interpreter->constants.push_back(string_value);
    emit(OpCode::CONST_, interpreter->constants.size() - 1);
    return expect(TokenType::String);
}

void Compiler::unary(OpCode op) {
    expression(P_Unary);

    emit(op);
}

void Compiler::block() {
    expect(L"{");
    while (!consume(L"}")) statement();
}

/// fun '(' args ')' block
void Compiler::function(Symbol* symbol, bool skip_args) {
    bool is_variadic = false;
    auto pos = cur.pos;
    push_scope();
    if (symbol != nullptr) { symbol_table->defineFun(symbol->name); }
    int nparam = 0;
    if (!skip_args) {
        expect(L"(");

        while (!consume(L")")) {
            check(TokenType::Identifier);
            nparam++;
            symbol_table->define(cur.literal);
            eat();

            if (consume(L"...")) {
                expect(L")");
                is_variadic = true;
                break;
            }
            if (consume(L")")) break;

            expect(L",");
        }
    }

    auto fun_debug_info = std::make_shared<DebugInfo>();
    fun_debug_info->filename = filename;
    get_error_pos(pos, fun_debug_info->position);
    auto old_debug_info = debug_info;
    debug_info = fun_debug_info.get();

    block();

    int line;
    get_error_pos(cur.pos, line);

    debug_info = old_debug_info;

    int nlocals = symbol_table->definitions;
    auto free_symbols = symbol_table->free;

    auto fun_instructions = pop_scope();
    if (fun_instructions->last_instruction == OpCode::POP) {
        fun_instructions->pop_back();
        fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
    } else if (fun_instructions->last_instruction != OpCode::RET) {
        fun_instructions->emit(fun_debug_info.get(), line, OpCode::CONST_NULL);
        fun_instructions->emit(fun_debug_info.get(), line, OpCode::RET);
    }

    for (auto const& i : free_symbols) { emit(OpCode::LOAD, i.scope, i.index); }

    static int function_count = 0;
    std::wstring id;
    if (symbol == nullptr) {
        id = std::to_wstring(function_count++);
    } else {
        id = symbol->name;
    }

    auto fun = new Function{FunctionType::Function, id,
            std::move(fun_instructions), nlocals, nparam, is_variadic,
            fun_debug_info};
    auto fun_value = SRCLANG_VALUE_FUNCTION(fun);
    interpreter->memoryManager.heap.push_back(fun_value);
    interpreter->constants.push_back(fun_value);

    emit(OpCode::CLOSURE, interpreter->constants.size() - 1,
            free_symbols.size());
}

/// list ::= '[' (<expression> % ',') ']'
void Compiler::list() {
    int size = 0;
    while (!consume(L"]")) {
        expression();
        size++;
        if (consume(L"]")) break;
        expect(L",");
    }
    emit(OpCode::PACK, size);
}

/// map ::= '{' ((<identifier> ':' <expression>) % ',') '}'
void Compiler::map_() {
    int size = 0;
    while (!consume(L"}")) {
        check(TokenType::Identifier);
        interpreter->constants.push_back(
                SRCLANG_VALUE_STRING(wchdup(cur.literal.c_str())));
        emit(OpCode::CONST_, interpreter->constants.size() - 1);
        eat();

        expect(L":");

        expression();
        size++;
        if (consume(L"}")) break;
        expect(L",");
    }
    emit(OpCode::MAP, size);
}

void Compiler::prefix(bool can_assign) {
    if (cur.type == TokenType::Number) {
        return number();
    } else if (cur.type == TokenType::String) {
        return string_();
    } else if (cur.type == TokenType::Identifier) {
        return identifier(can_assign);
    } else if (consume(L"not")) {
        return unary(OpCode::NOT);
    } else if (consume(L"-")) {
        return unary(OpCode::NEG);
    } else if (consume(L"[")) {
        return list();
    } else if (consume(L"{")) {
        return map_();
    } else if (consume(L"fun")) {
        return function(nullptr);
    } else if (consume(L"(")) {
        expression();
        return expect(L")");
    }

    error(L"Unknown expression type '" + SRCLANG_TOKEN_ID[int(cur.type)] + L"'",
            cur.pos);
}

void Compiler::binary(OpCode op, int prec) {
    int pos = 0;
    switch (op) {
    case OpCode::OR: pos = emit(OpCode::CHK, 1, 0); break;
    case OpCode::AND: pos = emit(OpCode::CHK, 0, 0); break;
    default: break;
    }
    expression(prec + 1);
    emit(op);
    if (op == OpCode::OR || op == OpCode::AND) {
        inst()->at(pos + 2) = inst()->size();
    }
}

/// call ::= '(' (expr % ',' ) ')'
void Compiler::call() {
    auto pos = cur.pos;
    int count = 0;
    while (!consume(L")")) {
        count++;
        expression();
        if (consume(L")")) break;
        expect(L",");
    }
    if (count >= UINT8_MAX) {
        error(L"can't have arguments more that '" + std::to_wstring(UINT8_MAX) +
                        L"'",
                pos);
    }
    emit(OpCode::CALL, count);
}

/// call ::= '{' map '}'
void Compiler::call2() {
    auto pos = cur.pos;
    map_();
    emit(OpCode::CALL, 1);
}

/// index ::= <expression> '[' <expession> (':' <expression>)? ']'
void Compiler::index(bool can_assign) {
    int count = 1;
    if (cur.literal == L":") {
        emit(OpCode::CONST_INT, 0);
    } else {
        expression();
    }

    if (consume(L":")) {
        count += 1;
        if (cur.literal == L"]") {
            emit(OpCode::CONST_INT, -1);
        } else {
            expression();
        }
    }
    expect(L"]");
    if (can_assign && consume(L"=") && count == 1) {
        value(nullptr);
        emit(OpCode::SET);
    } else {
        emit(OpCode::INDEX, count);
    }
}

/// subscript ::= <expression> '.' <expression>
void Compiler::subscript(bool can_assign) {
    check(TokenType::Identifier);

    auto string_value = SRCLANG_VALUE_STRING(wchdup(cur.literal.c_str()));
    interpreter->memoryManager.heap.push_back(string_value);
    interpreter->constants.push_back(string_value);
    emit(OpCode::CONST_, interpreter->constants.size() - 1);
    eat();

    if (can_assign && consume(L"=")) {
        value(nullptr);
        emit(OpCode::SET);
    } else {
        emit(OpCode::INDEX, 1);
    }
}

void Compiler::infix(bool can_assign) {
    static std::map<std::wstring, OpCode> binop = {
            {L"+", OpCode::ADD},
            {L"-", OpCode::SUB},
            {L"/", OpCode::DIV},
            {L"*", OpCode::MUL},
            {L"==", OpCode::EQ},
            {L"!=", OpCode::NE},
            {L"<", OpCode::LT},
            {L">", OpCode::GT},
            {L">=", OpCode::GE},
            {L"<=", OpCode::LE},
            {L"and", OpCode::AND},
            {L"or", OpCode::OR},
            {L"|", OpCode::LOR},
            {L"&", OpCode::LAND},
            {L">>", OpCode::LSHIFT},
            {L"<<", OpCode::RSHIFT},
            {L"%", OpCode::MOD},
    };

    if (consume(L"(")) {
        return call();
    } else if (consume(L"{")) {
        return call2();
    } else if (consume(L".")) {
        return subscript(can_assign);
    } else if (consume(L"[")) {
        return index(can_assign);
    } else if (binop.find(cur.literal) != binop.end()) {
        std::wstring op = cur.literal;
        eat();
        return binary(binop[op], precedence(op));
    }

    error(L"unexpected infix operation", cur.pos);
}

void Compiler::expression(int prec) {
    bool can_assign = prec <= P_Assignment;
    prefix(can_assign);

    while ((cur.literal != L";") && prec <= precedence(cur.literal)) {
        infix(can_assign);
    }
}

/// compiler_options ::= #![<option>(<value>)]
void Compiler::compiler_options() {
    expect(L"[");

    check(TokenType::Identifier);
    auto option_id = cur.literal;
    auto pos = cur.pos;
    eat();

    auto id = interpreter->options.find(option_id);
    if (id == interpreter->options.end()) {
        error(L"unknown compiler option '" + option_id + L"'", pos);
    }
#define CHECK_TYPE_ID(ty)                                                      \
    if (variant_typeid(id->second) != typeid(ty)) {                            \
        error(L"invalid value of type '" +                                     \
                        s2ws(variant_typeid(id->second).name()) +              \
                        L"' for option '" + option_id + L"', required '" +     \
                        s2ws(typeid(ty).name()) + L"'",                        \
                pos);                                                          \
    }
    Interpreter::OptionType value;
    if (consume(L"(")) {
        switch (cur.type) {
        case TokenType::Identifier:
            if (cur.literal == L"true" || cur.literal == L"false") {
                CHECK_TYPE_ID(bool);
                value = cur.literal == L"true";
            } else {
                CHECK_TYPE_ID(std::wstring);
                value = cur.literal;
            }
            break;
        case TokenType::String:
            CHECK_TYPE_ID(std::wstring);
            value = cur.literal;
            break;
        case TokenType::Number: {
            bool is_float = false;
            for (int i = 0; i < cur.literal.size(); i++)
                if (cur.literal[i] == '.') is_float = true;

            if (is_float) {
                CHECK_TYPE_ID(float);
                value = stof(cur.literal);
            } else {
                CHECK_TYPE_ID(int);
                value = stoi(cur.literal);
            }
        } break;
        default: CHECK_TYPE_ID(void);
        }
        eat();
        expect(L")");
    } else {
        CHECK_TYPE_ID(bool);
        value = true;
    }
#undef CHECK_TYPE_ID

    if (option_id == L"VERSION") {
        if (SRCLANG_VERSION > std::get<int>(value)) {
            error(L"Code need SrcLang of version above or equal to "
                  L"'" + std::to_wstring(SRCLANG_VERSION) +
                            L"'",
                    pos);
        }
    } else if (option_id == L"SEARCH_PATH") {
        interpreter->appendOption(
                option_id, std::get<std::wstring>(value), L":");
    } else {
        interpreter->setOption(option_id, value);
    }
    return expect(L"]");
}

/// let ::= 'let' 'global'? <identifier> '=' <expression>
void Compiler::let() {
    bool is_global = symbol_table->parent == nullptr;
    if (consume(L"global")) is_global = true;

    check(TokenType::Identifier);

    auto id = cur.literal;
    auto s = is_global ? global : symbol_table;
    auto symbol = s->resolve(id);
    if (symbol.has_value()) {
        error(L"Variable already defined '" + id + L"'", cur.pos);
    }
    symbol = s->define(id);

    eat();

    expect(L"=");

    value(&*symbol);

    emit(OpCode::STORE, symbol->scope, symbol->index);
    emit(OpCode::POP);
    return expect(L";");
}

void Compiler::return_() {
    expression();
    emit(OpCode::RET);
    return expect(L";");
}

void Compiler::patch_loop(int loop_start, OpCode to_patch, int pos) {
    for (int i = loop_start; i < inst()->size();) {
        auto j = OpCode(inst()->at(i++));
        switch (j) {
        case OpCode::CONTINUE:
        case OpCode::BREAK: {
            if (j == to_patch && inst()->at(i) == 0) inst()->at(i) = pos;
            i += SRCLANG_OPCODE_SIZE[int(j)];
        } break;

        default: i += SRCLANG_OPCODE_SIZE[int(j)]; break;
        }
    }
}

/// loop ::= 'for' <expression> <block>
/// loop ::= 'for' <identifier> 'in' <expression> <block>
void Compiler::loop() {
    expect(L"(");
    std::optional<Symbol> count, iter, temp_expr;
    static int loop_iterator = 0;
    static int temp_expr_count = 0;
    if (cur.type == TokenType::Identifier && peek.type == TokenType::Reserved &&
            peek.literal == L"in") {
        count = symbol_table->define(
                L"__iter_" + std::to_wstring(loop_iterator++) + L"__");
        temp_expr = symbol_table->define(
                L"__temp_expr_" + std::to_wstring(temp_expr_count++) + L"__");
        iter = symbol_table->resolve(cur.literal);
        if (iter == std::nullopt) iter = symbol_table->define(cur.literal);

        interpreter->constants.push_back(SRCLANG_VALUE_NUMBER(0));
        emit(OpCode::CONST_, interpreter->constants.size() - 1);
        emit(OpCode::STORE, count->scope, count->index);
        emit(OpCode::POP);
        eat();
        expect(L"in");
    }

    auto loop_start = inst()->size();
    expression();
    if (iter.has_value()) {
        emit(OpCode::STORE, temp_expr->scope, temp_expr->index);
    }

    int loop_exit;

    if (iter.has_value()) {
        emit(OpCode::SIZE);

        // perform index;
        emit(OpCode::LOAD, count->scope, count->index);
        emit(OpCode::GT);
        loop_exit = emit(OpCode::JNZ, 0);

        emit(OpCode::LOAD, temp_expr->scope, temp_expr->index);
        emit(OpCode::LOAD, count->scope, count->index);
        emit(OpCode::INDEX, 1);
        emit(OpCode::STORE, iter->scope, iter->index);
        emit(OpCode::POP);

        interpreter->constants.push_back(SRCLANG_VALUE_NUMBER(1));
        emit(OpCode::CONST_, interpreter->constants.size() - 1);
        emit(OpCode::LOAD, count->scope, count->index);
        emit(OpCode::ADD);
        emit(OpCode::STORE, count->scope, count->index);

        emit(OpCode::POP);
    } else {
        loop_exit = emit(OpCode::JNZ, 0);
    }

    expect(L")");

    block();

    patch_loop(loop_start, OpCode::CONTINUE, loop_start);

    emit(OpCode::JMP, loop_start);

    int after_condition = emit(OpCode::CONST_NULL);
    emit(OpCode::POP);

    inst()->at(loop_exit + 1) = after_condition;

    patch_loop(loop_start, OpCode::BREAK, after_condition);
}

void Compiler::import_() {
    auto path = cur;
    expect(TokenType::String);

    using convert_type = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_type, wchar_t> converter;

    std::wstring module_path = path.literal;
    if (!std::filesystem::path(module_path).has_extension()) {
        module_path += L".src";
    }

    try {
        module_path = interpreter->search(module_path);
    } catch (const std::exception& exception) {
        error(L"missing required module '" + path.literal + L"'", path.pos);
    }

    std::wifstream reader(converter.to_bytes(module_path));
    std::wstring input((std::istreambuf_iterator<wchar_t>(reader)),
            (std::istreambuf_iterator<wchar_t>()));
    reader.close();

    auto start_ = start;
    auto end_ = end;
    auto iter_ = iter;
    auto peek_ = peek;
    auto cur_ = cur;

    start = iter = input.begin();
    end = input.end();
    eat();
    eat();

    try {
        program();
    } catch (const std::exception& exception) {
        error(L"failed to import '" + module_path + L"'\n" +
                        s2ws(exception.what()),
                cur.pos);
    }

    start = start_;
    end = end_;
    cur = cur_;
    iter = iter_;
    peek = peek_;
}

/// condition ::= 'if' <expression> <block> (else statement)?
void Compiler::condition() {
    expect(L"(");
    do { expression(); } while (consume(L";"));
    expect(L")");

    auto false_pos = emit(OpCode::JNZ, 0);
    block();

    auto jmp_pos = emit(OpCode::JMP, 0);
    auto after_block_pos = inst()->size();

    inst()->at(false_pos + 1) = after_block_pos;

    if (consume(L"else")) {
        if (consume(L"if")) {
            condition();
        } else {
            block();
        }
    }

    auto after_alt_pos = emit(OpCode::CONST_NULL);
    emit(OpCode::POP);
    inst()->at(jmp_pos + 1) = after_alt_pos;
}

ValueType Compiler::type(std::string literal) {
    auto type = literal;
    auto iter = std::find(
            SRCLANG_VALUE_TYPE_ID.begin(), SRCLANG_VALUE_TYPE_ID.end(), type);
    if (iter == SRCLANG_VALUE_TYPE_ID.end()) {
        throw std::runtime_error("Invalid type '" + type + "'");
    }
    return SRCLANG_VALUE_AS_TYPE(
            SRCLANG_VALUE_TYPES[distance(SRCLANG_VALUE_TYPE_ID.begin(), iter)]);
};

void Compiler::statement() {
    if (consume(L"let"))
        return let();
    else if (consume(L"return"))
        return return_();
    else if (consume(L"import"))
        return import_();
    else if (consume(L"fun")) {
        auto id = cur;
        expect(TokenType::Identifier);
        auto symbol = symbol_table->resolve(id.literal);
        if (symbol) { error(L"function already defined", id.pos); }
        symbol = symbol_table->define(id.literal);
        function(&*symbol);
        emit(OpCode::STORE, symbol->scope, symbol->index);
        emit(OpCode::POP);
        return;
    } else if (consume(L";"))
        return;
    else if (consume(L"if"))
        return condition();
    else if (consume(L"for"))
        return loop();
    else if (consume(L"break")) {
        emit(OpCode::BREAK, 0);
        return;
    } else if (consume(L"continue")) {
        emit(OpCode::CONTINUE, 0);
        return;
    } else if (consume(L"defer")) {
        return defer();
    } else if (consume(L"#!")) {
        return compiler_options();
    }

    expression();
    emit(OpCode::POP);
    return expect(L";");
}

void Compiler::program() {
    while (cur.type != TokenType::Eof) { statement(); }
}

Compiler::Compiler(const std::wstring& source, const std::wstring& filename,
        Interpreter* interpreter, SymbolTable* symbol_table)
        : start(source.begin()), end(source.end()), interpreter(interpreter),
          filename(filename), global(symbol_table) {
    global_debug_info = std::make_shared<DebugInfo>();
    global_debug_info->filename = filename;
    global_debug_info->position = 0;
    debug_info = global_debug_info.get();
    this->symbol_table = global;
    iter = start;
    instructions.push_back(std::make_unique<Instructions>());
    eat();
    eat();
}

void Compiler::compile() {
    program();
    emit(OpCode::HLT);
}

void Compiler::value(Symbol* symbol) {
    if (consume(L"fun")) {
        return function(symbol);
    } else if (consume(L"native")) {
        return native(symbol);
    }
    return expression();
}

void Compiler::defer() {
    function(nullptr, true);
    emit(OpCode::DEFER);
    return expect(L";");
}

void Compiler::native(Symbol* symbol) {
    auto native = new Native{};
    native->id = symbol->name;

    if (cur.type == TokenType::Identifier) {
        native->id = cur.literal;
        eat();
    }

    expect(L"(");

    while (!consume(L")")) {
        check(TokenType::Identifier);
        try {
            native->parameters.push_back(
                    Native::fromString(ws2s(cur.literal).c_str()));
        } catch (const std::exception& e) { error(s2ws(e.what()), cur.pos); }

        eat();

        if (consume(L")")) break;
        expect(L",");
    }

    check(TokenType::Identifier);

    try {
        native->ret = Native::fromString(ws2s(cur.literal).c_str());
    } catch (const std::exception& exception) {
        error(s2ws(exception.what()), cur.pos);
    }
    eat();
    interpreter->constants.push_back(SRCLANG_VALUE_NATIVE(native));
    emit(OpCode::CONST_, interpreter->constants.size() - 1);
    emit(OpCode::STORE, symbol->scope, symbol->index);
}

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

Instructions* Compiler::inst() const { return instructions.back().get(); }

void Compiler::push_scope() {
    symbol_table = new SymbolTable(symbol_table);
    instructions.push_back(std::make_shared<Instructions>());
}

int Compiler::add_constant(Value value) const {
    interpreter->constants.push_back(value);
    return static_cast<int>(interpreter->constants.size() - 1);
}

std::shared_ptr<Instructions> Compiler::pop_scope() {
    auto const old = symbol_table;
    symbol_table = symbol_table->parent;
    delete old;
    auto res = instructions.back();
    instructions.pop_back();
    return res;
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
    return {err_pos, i};
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

void Compiler::check(TokenType type) const {
    if (cur.type != type) {
        std::wstringstream ss;
        ss << "Expected '" << s2ws(SRCLANG_TOKEN_ID[static_cast<int>(type)])
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
        ss << "Expected '" << s2ws(SRCLANG_TOKEN_ID[static_cast<int>(type)])
           << "' but got '" << cur.literal << "'";
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

    auto escape = [](Iterator& iterator) -> wchar_t {
        if (*iterator == L'\\') {
            auto const ch = *++iterator;
            ++iterator;
            switch (ch) {
            case L'a': return L'\a';
            case L'b': return L'\b';
            case L'n': return L'\n';
            case L't': return L'\t';
            case L'r': return L'\r';
            case L'\\': return L'\\';
            case L'\'': return L'\'';
            case L'"': return L'"';
            case L'0':
                if (*iterator == L'3' && *(iterator + 1) == L'3') {
                    iterator += 2;
                    return L'\033';
                }
            default: throw(L"invalid escape sequence", iterator - 1);
            }
        }
        return *iterator++;
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
        while (*iter != starting) {
            if (iter == end) { error(L"unterminated string", peek.pos); }
            try {
                peek.literal += escape(iter);
            } catch (const std::wstring& str) { error(str, iter); }
        }
        ++iter;
        peek.type = TokenType::String;
        return;
    }

    for (std::wstring k : {L"let", L"fun", L"native", L"return", L"class",
                 L"if", L"else", L"for", L"break", L"continue", L"require",
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

Compiler::Precedence Compiler::precedence(const std::wstring& tok) {
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
    std::wstring number_value;
    if (cur.literal.length() > 1 && cur.literal[0] == '0') {
        base = 8;
        cur.literal = cur.literal.substr(1);
    }
    for (auto i : cur.literal) {
        if (i == L'.') {
            if (is_float) {
                error(L"multiple floating point detected", cur.pos);
            }
            number_value += L'.';
            is_float = true;
        } else if (i == L'_') {
            continue;
        } else if (i == L'b') {
            base = 2;
        } else if (i == L'h') {
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
    if (auto const iter = std::find(SRCLANG_VALUE_TYPE_ID.begin(),
                SRCLANG_VALUE_TYPE_ID.end(), ws2s(cur.literal));
            iter != SRCLANG_VALUE_TYPE_ID.end()) {
        emit(OpCode::LOAD, Symbol::Scope::TYPE,
                distance(SRCLANG_VALUE_TYPE_ID.begin(), iter));
        return eat();
    }
    check(TokenType::Identifier);
    auto const id = cur;
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
    emit(OpCode::CONST_,
            add_constant(SRCLANG_VALUE_STRING(wcsdup(cur.literal.c_str()))));
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
    if (symbol != nullptr) { symbol_table->define_fun(symbol->name); }
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

    emit(OpCode::CLOSURE,
            add_constant(SRCLANG_VALUE_FUNCTION((new Function{
                    FunctionType::Function, id, std::move(fun_instructions),
                    nlocals, nparam, is_variadic, fun_debug_info}))),
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
        emit(OpCode::CONST_, add_constant(SRCLANG_VALUE_STRING(
                                     wcsdup(cur.literal.c_str()))));
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
    if (cur.type == TokenType::Number) return number();
    if (cur.type == TokenType::String) return string_();
    if (cur.type == TokenType::Identifier) return identifier(can_assign);
    if (consume(L"not")) return unary(OpCode::NOT);
    if (consume(L"-")) return unary(OpCode::NEG);
    if (consume(L"[")) return list();
    if (consume(L"{")) return map_();
    if (consume(L"fun")) return function(nullptr);
    if (consume(L"require")) return require();

    if (consume(L"(")) {
        expression();
        return expect(L")");
    }

    error(L"Unknown expression type '" +
                    s2ws(SRCLANG_TOKEN_ID[static_cast<int>(cur.type)]) + L"'",
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
    emit(OpCode::CONST_,
            add_constant(SRCLANG_VALUE_STRING(wcsdup(cur.literal.c_str()))));
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

    if (consume(L"(")) return call();
    if (consume(L"{")) return call2();
    if (consume(L".")) return subscript(can_assign);
    if (consume(L"[")) return index(can_assign);

    if (binop.find(cur.literal) != binop.end()) {
        std::wstring const op = cur.literal;
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
    auto const option_id = cur.literal;
    auto const pos = cur.pos;
    eat();

    auto const id = interpreter->options.find(option_id);
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
            for (const wchar_t i : cur.literal)
                if (i == '.') is_float = true;

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
        interpreter->prependOption(
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

        emit(OpCode::CONST_, add_constant(SRCLANG_VALUE_NUMBER(0)));
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

        emit(OpCode::CONST_, add_constant(SRCLANG_VALUE_NUMBER(1)));
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

static bool ends_with(const std::wstring& str, const std::wstring& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void Compiler::require() {
    expect(L"(");
    auto module_id = cur;
    expect(TokenType::String);
    expect(L")");

    std::wstring module_path;

    try {
        module_path = interpreter->search(module_id.literal);
    } catch (const std::exception& exception) {
        error(L"missing required module '" + module_id.literal + L"'" +
                        s2ws(exception.what()),
                module_id.pos);
    }

    if (ends_with(module_path, L".mod")) {
        emit(OpCode::CONST_, add_constant(SRCLANG_VALUE_STRING(
                                     wcsdup(module_path.c_str()))));
        emit(OpCode::MODULE);
        return;
    }

    if (std::find(loaded_imports.begin(), loaded_imports.end(), module_path) !=
            loaded_imports.end()) {
        return;
    }
    loaded_imports.push_back(module_path);

    std::wifstream reader(ws2s(module_path));
    reader.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
    std::wstringstream wss;
    wss << reader.rdbuf();
    auto input = wss.str();

    auto old_symbol_table = symbol_table;
    symbol_table = new SymbolTable{old_symbol_table};
    Compiler compiler(input, module_path, interpreter, symbol_table);
    compiler.symbol_table = symbol_table;
    Value module;
    try {
        module = compiler.compile();
    } catch (const std::exception& exception) {
        error(L"failed to import '" + module_path + L"'\n" +
                        s2ws(exception.what()),
                cur.pos);
    }

    auto closure = SRCLANG_VALUE_AS_CLOSURE(module);
    closure->fun->instructions->pop_back(); // pop OpCode::HLT

    int total = 0;
    // export symbols
    for (const auto& i : symbol_table->store) {
        closure->fun->instructions->emit(compiler.global_debug_info.get(), 0,
                OpCode::CONST_,
                add_constant(SRCLANG_VALUE_STRING(wcsdup(i.first.c_str()))));
        closure->fun->instructions->emit(compiler.global_debug_info.get(), 0,
                OpCode::LOAD, i.second.scope, i.second.index);
        total++;
    }
    int nlocals = symbol_table->definitions;
    auto nfree = symbol_table->free;
    delete symbol_table;
    symbol_table = old_symbol_table;
    closure->fun->instructions->emit(
            compiler.global_debug_info.get(), 0, OpCode::MAP, total);
    closure->fun->instructions->emit(
            compiler.global_debug_info.get(), 0, OpCode::RET);

    for (auto const& i : nfree) { emit(OpCode::LOAD, i.scope, i.index); }

    closure->fun->nlocals = nlocals;
    closure->fun->nparams = 0;
    emit(OpCode::CLOSURE, add_constant(SRCLANG_VALUE_FUNCTION(closure->fun)),
            nfree.size());
    free(closure);
    emit(OpCode::CALL, 0);
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

ValueType Compiler::type() {
    auto const ty = cur;
    expect(TokenType::Identifier);
    auto const iter = std::find_if(SRCLANG_VALUE_TYPE_ID.begin(),
            SRCLANG_VALUE_TYPE_ID.end(), [ty](const std::string& v) -> bool {
                return s2ws(v) == ty.literal;
            });
    if (iter == SRCLANG_VALUE_TYPE_ID.end()) {
        error(L"Invalid type " + ty.literal + L"'", ty.pos);
    }
    return SRCLANG_VALUE_AS_TYPE(
            SRCLANG_VALUE_TYPES[distance(SRCLANG_VALUE_TYPE_ID.begin(), iter)]);
};

void Compiler::statement() {
    if (consume(L"let")) return let();
    if (consume(L"return")) return return_();
    if (consume(L"fun")) {
        auto id = cur;
        expect(TokenType::Identifier);
        auto symbol = symbol_table->resolve(id.literal);
        if (symbol) { error(L"function already defined", id.pos); }
        symbol = symbol_table->define(id.literal);
        function(&*symbol);
        emit(OpCode::STORE, symbol->scope, symbol->index);
        emit(OpCode::POP);
        return;
    }
    if (consume(L";")) return;
    if (consume(L"if")) return condition();
    if (consume(L"for")) return loop();
    if (consume(L"break")) {
        emit(OpCode::BREAK, 0);
        return;
    }
    if (consume(L"continue")) {
        emit(OpCode::CONTINUE, 0);
        return;
    }
    if (consume(L"defer")) return defer();
    if (consume(L"#!")) return compiler_options();

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

Value Compiler::compile() {
    program();
    emit(OpCode::HLT);

    return SRCLANG_VALUE_CLOSURE((new Closure{
            new Function{FunctionType::Function, filename,
                    std::move(instructions.back()), symbol_table->definitions,
                    0, false, global_debug_info},
            {}}));
}

void Compiler::value(Symbol* symbol) {
    if (consume(L"fun")) return function(symbol);
    if (consume(L"native")) return native(symbol);

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
    emit(OpCode::CONST_, add_constant(SRCLANG_VALUE_NATIVE(native)));
    emit(OpCode::STORE, symbol->scope, symbol->index);
}

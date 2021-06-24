#ifndef __COMPILER__
#define __COMPILER__

#include <io.hh>
#include <utils/define.hh>
#include <boost/variant.hpp>

#include <set>
#include <vector>
#include <map>
#include <string>

#include <assert.h>

#include "../lang/ast.hh"
#include "../lang/code.hh"

namespace src::compiler
{
    using std::string;

    using src::lang::bytecode;
    using src::lang::token;

    using color = rlx::io::color;
    using level = rlx::io::debug_level;

    namespace io = rlx::io;
    namespace ast = src::lang::ast;

    class block
    {
    private:
        std::map<string, int> _vars;
        std::map<uint64_t, string> _calls;
        std::vector<int> &_code;
        uint64_t _address;
        uint64_t _size;
        uint _ac;

    public:
        block(std::vector<int> &code, uint ac)
            : _code(code),
              _address(code.size()),
              _size(0),
              _ac(ac)
        {
        }

        void append(int a)
        {
            _code.push_back(a);
            _size++;
        }

        void append(int a, int b)
        {
            _code.push_back(a);
            _code.push_back(b);
            _size += 2;
        }

        void append(int a, int b, int c)
        {
            _code.push_back(a);
            _code.push_back(b);
            _code.push_back(c);
            _size += 3;
        }

        int &operator[](uint64_t i)
        {
            return _code[_address + i];
        }

        int const &operator[](uint64_t i) const
        {
            return _code[_address + i];
        }

        DEFINE_GET_METHOD(uint64_t, size);
        DEFINE_GET_METHOD(uint64_t, address);

        DEFINE_GET_METHOD(uint, ac);

        int vars_size() const
        {
            return _vars.size();
        }

        int const *lookup(string const &name) const
        {
            auto iter = _vars.find(name);
            if (iter == _vars.end())
                return nullptr;

            return &iter->second;
        }

        void insert(string const &name)
        {
            int i = _vars.size();
            _vars[name] = i;
        }

        void link_to(string id, uint64_t addr)
        {
            _calls[addr] = id;
        }

        void emit(std::ostream &out) const
        {
            std::vector<int>::const_iterator pc = _code.begin() + _address;
            std::vector<string> locals(_vars.size());
            for (auto const &p : _vars)
            {
                locals[p.second] = p.first;
                out << ".local " << p.first << ", #" << p.second << std::endl;
            }

            std::map<size_t, string> lines;
            std::set<size_t> jumps;

            while (pc != (_code.begin() + _address + _size))
            {
                // io::println("PC: ", src::lang::bytecode_to_str(bytecode(*pc)), " address: ", _address);
                string line;
                size_t addr = pc - _code.begin();
                switch (*pc)
                {
                case bytecode::NEG:
                case bytecode::NOT:
                case bytecode::ADD:
                case bytecode::SUB:
                case bytecode::MUL:
                case bytecode::DIV:
                case bytecode::EQ:
                case bytecode::NE:
                case bytecode::LT:
                case bytecode::LE:
                case bytecode::GT:
                case bytecode::GE:
                case bytecode::AND:
                case bytecode::OR:
                case bytecode::RET:
                    line += io::format("\t", src::lang::bytecode_to_str(bytecode(*pc)));
                    ++pc;
                    break;

                case bytecode::LOAD:
                case bytecode::STORE:
                    line += io::format("\t", src::lang::bytecode_to_str(bytecode(*pc)), " ");
                    ++pc;
                    line += locals[*pc++];
                    break;

                case bytecode::INT:
                case bytecode::PTR:
                case bytecode::ADJ_STK:
                case bytecode::ALLOC:
                    line += io::format("\t", src::lang::bytecode_to_str(bytecode(*pc)), " ");
                    ++pc;
                    line += std::to_string(*pc++);
                    break;

                case bytecode::JMP:
                case bytecode::JMP_IF:
                    line += io::format("\t", src::lang::bytecode_to_str(bytecode(*pc)), " ");
                    pc++;
                    {
                        size_t pos = ((pc)-_code.begin()) + *pc++;
                        if (pos == _code.size())
                            line += "\n";
                        else
                            line += std::to_string(pos);
                        jumps.insert(pos);
                    }
                    break;

                case bytecode::CALL:
                    line += io::format("\t", src::lang::bytecode_to_str(bytecode(*pc)), " ");
                    pc++;
                    {
                        int nargs = *pc++;
                        std::size_t jmp = *pc++;
                        line += std::to_string(nargs) + ", ";
                        assert(_calls.find(jmp) != _calls.end());
                        line += _calls.find(jmp)->second;
                    }
                    break;
                default:
                    assert(0);
                }

                lines[addr] = line;
            }

            out << "entry: " << std::endl;
            for (auto const &i : lines)
            {
                size_t pos = i.first;
                if (jumps.find(pos) != jumps.end())
                    out << pos << ':' << std::endl;

                std::cout << i.second << std::endl;
            }

            out << std::endl;
        }
    };

    class codegen
    {
    private:
        compiler::block *cur;
        std::map<string, std::shared_ptr<compiler::block>> _fns;
        string cur_fn;
        std::vector<int> _code;
        std::vector<char> _data;

    public:
        typedef bool result_type;

        codegen()
            : cur(0)
        {
        }

        DEFINE_GET_METHOD(std::vector<int>, code);

        bool operator()(ast::nil)
        {
            assert(0);
            return false;
        }

        bool operator()(unsigned int x)
        {
            assert(cur != 0);
            cur->append(bytecode::INT, x);

            return true;
        }

        bool operator()(string const &x)
        {
            assert(cur != 0);
            auto start = _data.size();

            for (auto c : x)
                _data.push_back(c);

            auto end = _data.size();
            cur->append(bytecode::ALLOC, end - start);

            return true;
        }

        bool operator()(ast::ident const &x)
        {
            assert(cur != 0);

            int const *p = cur->lookup(x.id);
            if (p == 0)
            {
                std::cout << "Undefined variable: " << x.id << std::endl;
                return false;
            }

            cur->append(bytecode::LOAD, *p);
            return true;
        }

        bool operator()(ast::binary const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.lhs))
            {
                io::debug(level::debug, "lhs failed ", x.lhs.type().name());
                return false;
            }

            if (!boost::apply_visitor(*this, x.rhs))
            {
                io::debug(level::debug, "rhs failed ", x.rhs.type().name());
                return false;
            }

            switch (x.oper)
            {
            case token::PLUS:
                cur->append(bytecode::ADD);
                break;
            case token::MINUS:
                cur->append(bytecode::SUB);
                break;
            case token::MUL:
                cur->append(bytecode::MUL);
                break;
            case token::DIV:
                cur->append(bytecode::DIV);
                break;

            case token::EQ:
                cur->append(bytecode::EQ);
                break;
            case token::NE:
                cur->append(bytecode::NE);
                break;
            case token::LE:
                cur->append(bytecode::LE);
                break;
            case token::GE:
                cur->append(bytecode::GE);
                break;
            case token::LT:
                cur->append(bytecode::LT);
                break;
            case token::GT:
                cur->append(bytecode::GT);
                break;

            case token::AND:
                cur->append(bytecode::AND);
                break;
            case token::OR:
                cur->append(bytecode::OR);
                break;

            default:
                assert(0);
                return false;
            }

            return true;
        }

        bool operator()(ast::unary const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.lhs))
                return false;

            switch (x.oper)
            {
            case token::MINUS:
                cur->append(bytecode::NEG);
                break;
            case token::NOT:
                cur->append(bytecode::NOT);
                break;

            default:
                assert(0);
                return false;
            }

            return true;
        }

        bool operator()(ast::call const &x)
        {
            assert(cur != 0);
            string fn_id = boost::get<ast::ident>(x.id).id;

            if (_fns.find(fn_id) == _fns.end())
            {
                std::cout << "unknown function: " << fn_id << std::endl;
                return false;
            }

            auto p = _fns[fn_id];
            if (p->ac() != x.args.size())
            {
                std::cout << "wrong args " << fn_id << std::endl;
                return false;
            }

            for (auto const &i : x.args)
            {
                if (!boost::apply_visitor(*this, i))
                    return false;
            }

            cur->append(
                bytecode::CALL,
                p->ac(),
                p->address());

            cur->link_to(fn_id, p->address());
            return true;
        }

        bool operator()(ast::assign const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.val))
                return false;

            auto id = boost::get<ast::ident>(x.id).id;
            auto p = cur->lookup(id);

            if (p == 0)
            {
                std::cout << "undeclared var: " << id << std::endl;
                return false;
            }

            cur->append(bytecode::STORE, *p);
            return true;
        }

        bool operator()(ast::let const &x)
        {
            assert(cur != 0);

            auto id = x.var.ident_.id;

            auto p = cur->lookup(id);
            if (p != 0)
            {
                std::cout << "already declared var: " << id << std::endl;
                return false;
            }

            if (!boost::apply_visitor(*this, x.val))
                return false;

            cur->insert(id);
            auto loc = cur->size();
            if (cur[loc-2] == bytecode::INT)
                cur->append(bytecode::STORE, *cur->lookup(id));
            else if (cur[loc-2] == bytecode::ALLOC)
                cur->append(bytecode::)
            
            return true;
        }

        bool operator()(ast::block const &x)
        {
            assert(cur != 0);
            for (auto const &i : x)
                if (!(boost::apply_visitor(*this, i)))
                    return false;

            return true;
        }

        bool operator()(ast::condition const &x)
        {
            assert(cur != 0);
            if (!(boost::apply_visitor(*this, x.cond)))
                return false;

            cur->append(bytecode::JMP_IF, 0);
            auto skip = cur->size();

            if (!(boost::apply_visitor(*this, x.then_)))
                return false;

            (*cur)[skip] = cur->size() - skip;

            (*cur)[skip] += 2;
            cur->append(bytecode::JMP, 0);
            auto _exit = cur->size() - 1;
            if (!(boost::apply_visitor(*this)(x.else_)))
                return false;

            (*cur)[_exit] = cur->size() - _exit;

            return true;
        }

        bool operator()(ast::while_loop const &x)
        {
            assert(cur != 0);
            auto pos = cur->size();
            if (!boost::apply_visitor(*this, x.cond))
                return false;

            cur->append(bytecode::JMP_IF, 0);
            auto _exit = cur->size() - 1;
            if (!boost::apply_visitor(*this, x.body))
                return false;

            cur->append(bytecode::JMP, int(pos - 1) - int(cur->size()));
            (*cur)[_exit] = cur->size() - _exit;

            return true;
        }

        bool operator()(ast::for_loop const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.init))
                return false;

            auto pos = cur->size();
            if (!boost::apply_visitor(*this, x.cond))
                return false;

            cur->append(bytecode::JMP_IF, 0);
            auto _exit = cur->size() - 1;

            if (!boost::apply_visitor(*this, x.body))
                return false;

            if (!boost::apply_visitor(*this, x.inc))
                return false;

            cur->append(bytecode::JMP, int(pos - 1) - int(cur->size()));
            (*cur)[_exit] = cur->size() - _exit;

            return true;
        }

        bool operator()(ast::expr_stmt const &x)
        {
            return boost::apply_visitor(*this, x.expr_);
        }

        bool operator()(ast::ret const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.val))
                return false;

            cur->append(bytecode::RET);
            return true;
        }

        bool operator()(ast::fn const &x)
        {
            if (_fns.find(x.proto_.id.id) != _fns.end())
            {
                std::cout << "duplicate fn: " << x.proto_.id.id << std::endl;
                return false;
            }

            _fns[x.proto_.id.id].reset();
            _fns[x.proto_.id.id] = std::make_shared<compiler::block>(_code, x.proto_.args.size());
            cur = _fns[x.proto_.id.id].get();
            cur_fn = x.proto_.id.id;

            cur->append(bytecode::ADJ_STK, 0);
            for (auto const &i : x.proto_.args)
                cur->insert(i.ident_.id);

            if (!(*this)(x.body))
                return false;

            (*cur)[1] = cur->vars_size();

            return true;
        }

        bool operator()(ast::root_decls const &x)
        {
            _code.push_back(src_code);
            _code.push_back(0);
            _code.push_back(bytecode::JMP);
            _code.push_back(0);

            for (auto const &i : x)
                if (!boost::apply_visitor(*this, i))
                {
                    _code.clear();
                    return false;
                }

            _code[1] = _code.size();

            return true;
        }

        std::shared_ptr<compiler::block>
        getfn(string const &id) const
        {
            auto f = _fns.find(id);
            if (f == _fns.end())
                return nullptr;
            else
                return f->second;
        }

        bool dump(string f)
        {
            auto file = fopen(f.c_str(), "wb");
            if (file == nullptr)
            {
                std::cout << "error creating file " << f << std::endl;
                return false;
            }

            for (auto i : _code)
                fwrite((const void *)&i, sizeof(int), 1, file);

            fclose(file);

            return true;
        }

        void emit(std::ostream &out)
        {
            for (auto p : _fns)
            {
                assert(p.second != nullptr);
                out << p.second->address() << ": fn " << p.first << std::endl;
                p.second->emit(out);

                out << "data: " << std::endl;
                for(auto i : _data)
                    out << i;
                out << std::endl;
            }
        }

        template <typename T>
        bool operator()(T)
        {
            throw std::runtime_error("not yet implemented for " + std::string(typeid(T).name()));
            return false;
        }
    };
}

#endif
#ifndef __COMPILER__
#define __COMPILER__

#include <utils/define.hh>
#include <boost/variant.hpp>

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
    using src::lang::tokentype;

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
    };

    class codegen
    {
    private:
        compiler::block *cur;
        std::map<string, std::shared_ptr<compiler::block>> _fns;
        string cur_fn;
        std::vector<int> _code;

    public:
        typedef bool result_type;

        codegen()
            : cur(0)
        {
        }

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
            return false;
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
            return false;
        }

        bool operator()(ast::binary const &x)
        {
            assert(cur != 0);
            if (!boost::apply_visitor(*this, x.lhs))
                return false;

            if (!boost::apply_visitor(*this, x.rhs))
                return false;

            switch (x.oper)
            {
            case '+':
                cur->append(bytecode::ADD);
                break;
            case '-':
                cur->append(bytecode::SUB);
                break;
            case '*':
                cur->append(bytecode::MUL);
                break;
            case '/':
                cur->append(bytecode::DIV);
                break;

            case tokentype::eq:
                cur->append(bytecode::EQ);
                break;
            case tokentype::ne:
                cur->append(bytecode::NE);
                break;
            case tokentype::le:
                cur->append(bytecode::LE);
                break;
            case tokentype::ge:
                cur->append(bytecode::GE);
                break;
            case '<':
                cur->append(bytecode::LT);
                break;
            case '>':
                cur->append(bytecode::GT);
                break;

            case tokentype::and_:
                cur->append(bytecode::AND);
                break;
            case tokentype::or_:
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
            case '-':
                cur->append(bytecode::NEG);
                break;
            case tokentype::not_:
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
            cur->append(bytecode::STORE, *cur->lookup(id));
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

            auto p = _fns[x.proto_.id.id];
            p.reset(new compiler::block(_code, x.proto_.args.size()));
            cur = p.get();
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

        template <typename T>
        bool operator()(T)
        {
            throw std::runtime_error("not yet implemented for " + std::string(typeid(T).name()));
            return false;
        }
    };
}

#endif
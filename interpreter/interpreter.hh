#ifndef __COMPILER__
#define __COMPILER__

#include <io.hh>
#include <utils/define.hh>
#include <boost/variant.hpp>
#include <boost/type_traits.hpp>

#include <set>
#include <vector>
#include <map>
#include <string>

#include <assert.h>
#include "../lang/ast.hh"
#include <utils/define.hh>

namespace src::interpreter
{
    using std::string;

    using src::lang::token;

    using color = rlx::io::color;
    using level = rlx::io::debug_level;

    namespace io = rlx::io;
    namespace ast = src::lang::ast;

    struct nil
    {
    };
    struct break_
    {
    };
    struct continue_
    {
    };
    class fn;
    class array;
    class builtin;
    typedef boost::variant<
        nil,
        int,
        float,
        bool,
        std::string,
        break_,
        continue_,
        boost::recursive_wrapper<fn>,
        boost::recursive_wrapper<array>,
        boost::recursive_wrapper<builtin>>
        value;

    class array : public std::vector<value>
    {
    };

    class builtin
    {
    public:
        using infn = value (*)(std::vector<value> &args);

    private:
        infn _f;

    public:
        builtin(infn _f)
            : _f(_f)
        {
        }

        infn const &get()
        {
            return _f;
        }
    };

    class context;
    class fn
    {
    private:
        std::vector<ast::ident> _args;
        ast::stmt _body;
        context *_env;

    public:
        fn(
            std::vector<ast::ident> const &args,
            ast::stmt const &body,
            context *env)
            : _args(args), _body(body), _env(env)

        {
        }

        DEFINE_GET_METHOD(std::vector<ast::ident>, args)
        DEFINE_GET_METHOD(ast::stmt, body)

        context *env()
        {
            return _env;
        }
    };

    class istruth : public boost::static_visitor<bool>
    {
    public:
        bool operator()(int x) const
        {
            return x != 0;
        }

        bool operator()(float x) const
        {
            return x != 0.0;
        }

        bool operator()(bool x) const
        {
            return x;
        }

        bool operator()(std::string const &x) const
        {
            return x.length() != 0;
        }

        template <typename T>
        bool operator()(T) const
        {
            return false;
        }
    };

    class calculator
    {
    private:
        value _value;
        token oper;

    public:
        class exception : public std::exception
        {
        private:
            string _what;

        public:
            exception(string const &w)
                : _what(w)
            {
            }

            const char *what() const noexcept
            {
                return _what.c_str();
            }
        };
        calculator(value const &v, token oper)
            : _value(v), oper(oper)
        {
        }

        value get()
        {
            return _value;
        }

        void operator()(int x)
        {
            if (_value.type().hash_code() != typeid(x).hash_code())
                throw calculator::exception("can not compute value of different type");

            switch (oper)
            {
            case token::PLUS:
                _value = boost::get<int>(_value) + x;
                break;

            case token::MINUS:
                _value = boost::get<int>(_value) - x;
                break;

            case token::MUL:
                _value = boost::get<int>(_value) * x;
                break;

            case token::DIV:
                _value = boost::get<int>(_value) / x;
                break;

            case token::EQ:
                _value = (bool)(boost::get<int>(_value) == x);
                break;

            case token::NE:
                _value = (bool)(boost::get<int>(_value) != x);
                break;

            case token::LT:
                _value = (bool)(boost::get<int>(_value) > x);
                break;

            case token::LE:
                _value = (bool)(boost::get<int>(_value) <= x);
                break;

            case token::GT:
                _value = (bool)(boost::get<int>(_value) > x);
                break;

            case token::GE:
                _value = (bool)(boost::get<int>(_value) >= x);
                break;

            default:
                throw calculator::exception("illegal operator provided ");
            }
        }

        template <typename T>
        void operator()(T t)
        {
            throw calculator::exception("not yet implemented for " + string(typeid(t).name()));
        }
    };

    class printer : public boost::static_visitor<string>
    {
    public:
        string operator()(nil) const
        {
            return "nil";
        }
        string operator()(int x) const
        {
            return std::to_string(x);
        }

        string operator()(float x) const
        {
            return std::to_string(x);
        }

        string operator()(bool x) const
        {
            return x ? "true" : "false";
        }
        string operator()(std::string const &x) const
        {
            return x;
        }

        string operator()(array const &x) const
        {
            string s = "[";
            string sep;
            for (auto const &i : x)
            {
                s += sep + boost::apply_visitor(*this, i);
                sep = ",";
            }
            return s + "]";
        }

        template <typename T>
        string operator()(T) const
        {
            return "<unknown>";
        }
    };

    static value builtin_println(std::vector<value> &args)
    {
        for (auto const &i : args)
            std::cout << boost::apply_visitor(printer(), i);
        std::cout << std::endl;

        return (int)args.size();
    }
    class context
    {
    public:
        class exception : public std::exception
        {
            string _what;

        public:
            exception(string const &w)
                : _what(w)
            {
            }

            const char *what() const noexcept
            {
                return _what.c_str();
            }
        };

    private:
        std::map<std::string, value> _store;
        context *parent = nullptr;

    public:
        context(context *parent = nullptr)
            : parent(parent)
        {
            insert("println", builtin(builtin_println));
        }

        bool is_defined(std::string const &id) const
        {
            auto iter = _store.find(id);
            if (iter == _store.end())
                if (parent)
                    return parent->is_defined(id);
                else
                    return false;

            return true;
        }

        value &lookup(std::string const &s)
        {
            auto iter = _store.find(s);
            if (iter == _store.end())
                if (parent)
                    return parent->lookup(s);
                else
                    throw context::exception("undefined variable " + s);

            return iter->second;
        }

        void insert(std::string const &id, value v)
        {
            try
            {
                lookup(id) = v;
            }
            catch (context::exception const &e)
            {
                _store[id] = v;
            }
        }
    };

    class eval
    {
    public:
        size_t _fn_count = 0;
        size_t _loop_count = 0;

        class exception : public std::exception
        {
            string _what;

        public:
            exception(string const &w)
                : _what(w)
            {
            }

            const char *what() const noexcept
            {
                return _what.c_str();
            }
        };

    private:
    public:
        context *cur = 0;

        eval(context *c)
            : cur(c)
        {
        }

        value operator()(ast::nil)
        {
            return value();
        }

        value operator()(unsigned int x)
        {
            return (int)x;
        }

        value operator()(std::string x)
        {
            return value(x);
        }

        value operator()(ast::ident const &x)
        {
            assert(cur != 0);
            return cur->lookup(x.id);
        }

        value operator()(ast::array const &x)
        {
            assert(cur != 0);
            array a;
            for (auto const &i : x)
                a.push_back(boost::apply_visitor(*this, i));

            return a;
        }

        value operator()(ast::binary const &x)
        {
            assert(cur != 0);
            auto cal = calculator(boost::apply_visitor(*this, x.lhs), x.oper);
            boost::apply_visitor(cal, boost::apply_visitor(*this, x.rhs));

            return cal.get();
        }

        value operator()(ast::unary const &x)
        {
            assert(cur != 0);

            return true;
        }

        value operator()(ast::call const &x)
        {
            assert(cur != 0);
            auto f = boost::apply_visitor(*this, x.id);
            if (f.type().hash_code() != typeid(fn).hash_code() &&
                f.type().hash_code() != typeid(builtin).hash_code())
                throw eval::exception("can't call value of type " + string(f.type().name()));

            if (f.type().hash_code() == typeid(builtin).hash_code())
            {
                std::vector<value> args;
                for (auto const &i : x.args)
                    args.push_back(boost::apply_visitor(*this, i));

                auto _bfn = boost::get<builtin>(f);
                return _bfn.get()(args);
            }
            auto _fn = boost::get<fn>(f);

            auto _sub_cc = _fn.env();
            if (_fn.args().size() != x.args.size())
                throw eval::exception("arguments count is not same");

            for (size_t i = 0, e = _fn.args().size(); i < e; i++)
            {
                auto v = boost::apply_visitor(*this, x.args[i]);
                _sub_cc->insert(_fn.args()[i].id, v);
            }

            auto _old_cc = cur;
            cur = _sub_cc;
            _fn_count++;
            value v = boost::apply_visitor(*this, _fn.body());
            _fn_count--;

            return v;
        }

        value operator()(ast::assign const &x)
        {
            assert(cur != 0);
            auto val = boost::apply_visitor(*this, x.val);
            if (x.id.type().hash_code() == typeid(ast::ident).hash_code())
            {
                string id = boost::get<ast::ident>(x.id).id;
                if (!cur->is_defined(id))
                    throw eval::exception(id + " is not defined");
                cur->insert(id, val);
                return value();
            }

            throw eval::exception("invalid type " + string(x.id.type().name()));
        }

        value operator()(ast::let const &x)
        {
            assert(cur != 0);
            auto id = x.var.id;
            if (cur->is_defined(id))
                throw eval::exception(id + " is not defined");

            auto val = boost::apply_visitor(*this, x.val);
            cur->insert(id, val);
            return true;
        }

        value operator()(ast::block const &x)
        {
            assert(cur != 0);
            value v;
            for (auto i : x)
            {
                v = boost::apply_visitor(*this, i);
                if (i.type().hash_code() == typeid(ast::ret).hash_code())
                    if (_fn_count)
                        return v;
                    else
                        throw eval::exception("can't return value outside a method");
                else if (v.type().hash_code() == typeid(break_).hash_code() ||
                         v.type().hash_code() == typeid(continue_).hash_code())
                    if (_loop_count)
                        return v;
                    else
                        throw eval::exception("can't use break/continue outside a loop");
            }

            return v;
        }

        value
        operator()(ast::condition const &x)
        {
            assert(cur != 0);

            auto cond = boost::apply_visitor(*this, x.cond);
            if (boost::apply_visitor(istruth(), cond))
                return boost::apply_visitor(*this, x.then_);

            return boost::apply_visitor(*this, x.else_);
        }

        value operator()(ast::while_loop const &x)
        {
            assert(cur != 0);
            _loop_count++;
            do
            {
                auto cond = boost::apply_visitor(*this, x.cond);
                if (!boost::apply_visitor(istruth(), cond))
                    break;

                auto val = boost::apply_visitor(*this, x.body);
                if (val.type().hash_code() == typeid(break_).hash_code())
                    break;

            } while (true);
            _loop_count--;

            return true;
        }

        value operator()(ast::for_loop const &x)
        {
            assert(cur != 0);
            _loop_count++;
            boost::apply_visitor(*this, x.init);

            do
            {
                auto cond = boost::apply_visitor(*this, x.cond);
                if (!boost::apply_visitor(istruth(), cond))
                    break;

                boost::apply_visitor(*this, x.body);
                boost::apply_visitor(*this, x.inc);

            } while (true);
            _loop_count--;

            return true;
        }

        value operator()(ast::break_ const &x)
        {
            return break_{};
        }

        value operator()(ast::continue_ const &x)
        {
            return continue_{};
        }

        value operator()(ast::expr_stmt const &x)
        {
            return boost::apply_visitor(*this, x.expr_);
        }

        value operator()(ast::ret const &x)
        {
            assert(cur != 0);

            return boost::apply_visitor(*this, x.val);
        }

        value operator()(ast::fn const &x)
        {
            assert(cur != 0);
            value v = fn(x.proto_.args, x.body, cur);
            cur->insert(x.proto_.id.id, v);
            return nil{};
        }

        template <typename T>
        value operator()(T)
        {
            throw eval::exception("not yet implemented for " + std::string(typeid(T).name()));
            return false;
        }
    };
}

#endif
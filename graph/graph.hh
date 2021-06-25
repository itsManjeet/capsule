#ifndef __GRAPH__
#define __GRAPH__

#include <boost/variant/recursive_variant.hpp>
#include <vector>
#include <string>

#include <rlx.hh>
#include "../lang/ast.hh"

namespace src::backend
{

    using namespace src::lang;

    class graph
    {
    private:
        std::ostream &out;
        size_t _id = 0;

        size_t id(string const &label)
        {
            io::fprint(out, "\t", ++_id, " [label=\"", label, "\"];\n");
            return ++_id;
        }

        void join(size_t a, size_t b)
        {
            io::fprint(out, "\t", a, " -> ", b, ";\n");
        }

    public:
        typedef int result_type;

        graph(std::ostream &os)
            : out(os)
        {
            io::fprint(out, "digraph {\n");
        }

        ~graph()
        {
            io::fprint(out, "}");
        }

        int operator()(ast::root_decls const &x)
        {

            io::debug(level::trace, "graphing root decls");

            auto root = id("start");
            for (auto const &i : x)
            {
                int decl = boost::apply_visitor(*this, i);
                join(root, decl);
            }

            return root;
        }

        int operator()(ast::proto const &x)
        {
            io::debug(level::trace, "graphing proto");
            auto root = id("proto - " + x.id.id + (x.is_variadic ? " ... " : ""));

            auto vars = id("var");
            join(root, vars);
            for (auto const &i : x.args)
            {
                int var = (*this)(i);
                join(vars, var);
            }

            auto ty = (*this)(x.type_);
            join(root, ty);

            return root;
        }

        int operator()(ast::fn const &x)
        {
            io::debug(level::trace, "graphing fn");
            (*this)(x.proto_);

            io::print("-");
            boost::apply_visitor(*this, x.body);
        }

        template <typename T>
        int operator()(T const &)
        {
            throw std::runtime_error("graph not yet implemented for '" + std::string(typeid(T).name()));
        }
    };
}

#endif
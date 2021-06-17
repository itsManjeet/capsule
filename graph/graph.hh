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
        size_t _id = 0;

        size_t id()
        {
            return ++_id;
        }


    public:
        graph()
        {
        }

        void operator()(ast::root_decls const &x)
        {
            io::debug(level::trace, "graphing root decls");
            for (auto const &i : x)
            {
                (*this)(i);
                io::println("");
            }
        }

        void operator()(ast::proto const &x)
        {
            io::debug(level::trace, "graphing proto");
            io::print(
                "[", "proto:", x.id.id,
                ":", x.type_.value.id, "]");
        }

        void operator()(ast::fn const &x)
        {
            io::debug(level::trace, "graphing fn");
            (*this)(x.proto_);

            io::print("-");
            boost::apply_visitor(*this, x.body);
        }

        template <typename T>
        void operator()(T const &)
        {
            throw std::runtime_error("graph not yet implemented for '" + std::string(typeid(T).name()));
        }
    };
}

#endif
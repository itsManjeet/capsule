#ifndef __AST__
#define __AST__

#include <string>
#include <boost/variant/recursive_variant.hpp>

namespace src::lang::ast
{
    struct nil
    {
    };

    struct ident
    {
        std::string id;
    };

    struct binary;
    struct unary;
    struct call;
    struct assign;
    struct array;
    struct index;
    typedef boost::variant<
        nil,
        unsigned int,
        std::string,
        ident,
        boost::recursive_wrapper<binary>,
        boost::recursive_wrapper<unary>,
        boost::recursive_wrapper<call>,
        boost::recursive_wrapper<array>,
        boost::recursive_wrapper<assign>,
        boost::recursive_wrapper<index>>
        expr;

    struct binary
    {
        char oper;
        expr lhs;
        expr rhs;
    };

    struct unary
    {
        char oper;
        expr lhs;
    };

    struct call
    {
        expr id;
        std::vector<expr> args;
    };

    struct array : std::vector<expr>
    {
    };

    struct index
    {
        expr id;
        expr val;
    };

    struct assign
    {
        expr id;
        expr val;
    };

    struct datatype
    {
        ident value;
    };

    struct variable
    {
        datatype type_;
        std::vector<int> args;
        ident ident_;
        unsigned int size = 1;
    };

    struct expr_stmt
    {
        expr expr_;
    };

    struct let
    {
        variable var;
        expr val;
    };

    struct ret
    {
        expr val;
    };

    struct condition;
    struct for_loop;
    struct while_loop;
    struct proto;
    struct fn;
    struct block;

    typedef boost::variant<
        nil,
        expr_stmt,
        let,
        ret,
        boost::recursive_wrapper<condition>,
        boost::recursive_wrapper<for_loop>,
        boost::recursive_wrapper<while_loop>,
        boost::recursive_wrapper<block>>
        stmt;

    struct block : std::vector<stmt>
    {
    };

    struct condition
    {
        expr cond;
        stmt then_, else_;
    };

    struct while_loop
    {
        expr cond;
        stmt body;
    };

    struct for_loop
    {
        expr cond;
        stmt init, inc, body;
    };

    struct use
    {
        std::string path;
    };

    struct proto
    {
        datatype type_;
        ident id;
        std::vector<variable> args;
        bool is_variadic = false;
    };

    struct fn
    {
        proto proto_;
        block body;
    };

    struct struct_
    {
        ident id;
        std::vector<variable> vars;
    };

    typedef boost::variant<proto, fn, use, struct_>
        root_decl;

    typedef std::vector<root_decl> root_decls;
}

#endif
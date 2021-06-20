#include "lang/lexer.hh"
#include "lang/parser.hh"
#include "compiler/compiler.hh"
#include "interpreter/interpreter.hh"

#include <io.hh>
#include <cli/cli.hh>
#include <utils/exec.hh>

#include <iostream>
#include <fstream>

using iterator = std::string::const_iterator;

std::string readfile(std::string const &path)
{
    std::ifstream file(path);
    std::string input(
        (std::istreambuf_iterator<char>(file)),
        (std::istreambuf_iterator<char>()));

    return input;
}

int main(int ac, char **av)
{
    namespace io = rlx::io;
    using level = rlx::io::debug_level;
    using color = rlx::io::color;
    using namespace rlx::cli;

    return app::create("src")
        .version("0.1.0")
        .about("A memory safe programming language")
        .usage("<filename> <Args>")

        .arg(arg::create("help")
                 .long_id("help")
                 .short_id('h')
                 .fn([](context const &cc) -> int
                     {
                         io::println(*cc.getapp());
                         return 0;
                     }))

        .arg(arg::create("output")
            .long_id("output")
            .short_id('o')
            .about("specify output name")
            .required(true))

        .arg(arg::create("lex")
            .long_id("lex")
            .about("perform lexical analysis of input stream"))

        .sub(app::create("run")
            .about("interpreter bytecodes")
            .fn([](context const& cc) -> int
            {
                if (cc.args().size() == 0)
                {
                    io::error("no input file provided");
                    return 1;
                }

                auto filename = cc.args()[0];

                FILE* file = fopen(filename.c_str(), "rb");
                if (file == nullptr)
                {
                    io::error("failed to read file");
                    return 1;
                }

                int magic_no, code_size, bytecode;
                fread(&magic_no, sizeof(int), 1, file);
                if (magic_no != src_code)
                {
                    io::error("not a src byte code file");
                    return 1;
                }

                fread(&code_size, sizeof(int), 1, file);
                
                std::vector<int> code;
                for(int i = 0; i < code_size; i++)
                {
                    fread(&bytecode, sizeof(int), 1, file);
                    code.push_back(bytecode);
                }

                auto interpreter = src::interpreter::machine();
                return interpreter.execute(code);
            }))


        .fn([](context const &cc) -> int
            {
                if (cc.args().size() == 0)
                {
                    io::error("no input file provided");
                    return 1;
                }

                auto filename = cc.args()[0];
                auto input = readfile(filename);

                iterator start = input.begin(),
                         end = input.end();

                auto lexer = src::lang::lexer<iterator>(start, end);
                if (cc.checkflag("lex"))
                {
                    io::debug(level::debug, "Lexical Analysis mode on");
                    int t;
                    do
                    {
                        t = lexer.eat_token();
                        if (t == src::lang::ident)
                            io::print("[ident:", lexer.ident(), "]");
                        else if (t == src::lang::number)
                            io::print("[num:", lexer.num(), "]");
                        else if (t == src::lang::str)
                            io::print("[str:", lexer.ident(), "]");
                        else
                            io::print("[", src::lang::tokentostr(t), "]");
                    } while (t != src::lang::eof);

                    return 0;
                }
                auto parser = src::lang::parser<iterator>(lexer);

                src::lang::ast::root_decls ast;

                try
                {
                    ast = parser.parse();
                }
                catch (std::runtime_error const &e)
                {
                    io::error("Parsing failed ", e.what());
                    return 0;
                }

                auto compiler = src::compiler::codegen();
                if (!compiler(ast))
                    return 1;

                if (!compiler.dump(cc.value("output","a.out")))
                    return 1;
                
                return 0;
            })

        .args(ac, av)
        .exec();
}
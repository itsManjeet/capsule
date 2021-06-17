#include "lang/lexer.hh"
#include "lang/parser.hh"
#include "compiler/compiler.hh"

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
            .fn([](context const& cc) -> int 
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

        .arg(arg::create("emit-ir")
                 .long_id("emit-ir")
                 .about("emit llvm itermediate representation"))

        .arg(arg::create("emit-bc")
                 .long_id("emit-bc")
                 .about("emit bytecode"))

        .arg(arg::create("gcc-flags")
                 .long_id("gcc-flags")
                 .about("add gcc flags for compilation")
                 .required(true))

        .arg(arg::create("llc-flags")
                 .long_id("llc-flags")
                 .about("add lc flags")
                 .required(true))

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
                            io::println("[ident:", lexer.ident(), "]");
                        else if (t == src::lang::number)
                            io::println("[num:", lexer.num(), "]");
                        else if (t == src::lang::str)
                            io::println("[str:", lexer.ident(), "]");
                        else
                            io::println("[", src::lang::tokentostr(t), "]");
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

                auto compiler = src::backend::compiler();

                try
                {
                    compiler.compile(ast);
                }
                catch (const std::exception &e)
                {
                    std::cerr << e.what() << '\n';
                    return 1;
                }

                if (cc.checkflag("emit-ir"))
                {
                    compiler.module_->print(llvm::errs(), nullptr);
                    return 0;
                }

                std::error_code ec;
                llvm::raw_fd_ostream irf(filename + ".ir", ec);

                compiler.module_->print(irf, nullptr);
                irf.flush();

                io::success("written ir to ", filename, ".ir");
                

                if (rlx::utils::exec::command("llc " + cc.value("llc-flags","-filetype=obj") +" " + filename + ".ir -o " + filename + ".bc"))
                {
                    io::error("failed to generate bytecode");
                    return 1;
                }

                io::success("byte code generated at ", filename + ".bc");
                std::filesystem::remove(filename + ".ir");

                if (cc.checkflag("emit-bc"))
                    return 0;

                if (rlx::utils::exec::command("gcc  " + cc.value("gcc-flags", "") + " " + filename + ".bc -o " + cc.value("output", "a.out")))
                {
                    io::error("failed to generate executable");
                    return 1;
                }

                io::success("executable generated at ", cc.value("output", "a.out"));
                std::filesystem::remove(filename + ".bc");

                return 0;
            })

        .args(ac, av)
        .exec();
}
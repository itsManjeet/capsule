#include "lang/lexer.hh"
#include "lang/parser.hh"
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

        .arg(arg::create("interactive").long_id("interactive").short_id('i').about("start src in interactive mode"))

        .fn([](context const &cc) -> int
            {
                using ast = src::lang::ast::block;

                bool is_interactive = cc.checkflag("interactive");

                if (!is_interactive && cc.args().size() == 0)
                    is_interactive = true;

                auto context = src::interpreter::context();

                do
                {
                    string input;

                    if (is_interactive)
                    {
                        std::cout << ">> ";
                        if (!getline(std::cin, input))
                            continue;
                    }
                    else
                        input = readfile(cc.args()[0]);

                    iterator
                        start =
                            input.begin(),
                        end =
                            input.end();

                    auto lexer = src::lang::lexer<iterator>(start, end);
                    auto parser = src::lang::parser(lexer);
                    auto interpreter = src::interpreter::eval(&context);
                    ast _ast;
                    try
                    {
                        _ast = parser.parse();
                        auto value = interpreter(_ast);
                        if (is_interactive)
                            io::println(boost::apply_visitor(src::interpreter::printer(), value));
                    }
                    catch (src::lang::lexer<iterator>::exception e)
                    {
                        io::message(color::RED, "Parsing failed", e.what());
                        if (!is_interactive)
                            return 1;
                    }
                    catch (src::interpreter::eval::exception e)
                    {
                        io::message(color::RED, "InterpreterError", e.what());
                        if (!is_interactive)
                            return 1;
                    }
                    catch (src::interpreter::context::exception e)
                    {
                        io::message(color::RED, "ContextError", e.what());
                        if (!is_interactive)
                            return 1;
                    }

                } while (is_interactive);
                return 0;
            })

        .args(ac, av)
        .exec();
}
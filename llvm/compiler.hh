#ifndef __COMPILER__
#define __COMPILER__

#include "../lang/ast.hh"
#include "../lang/parser.hh"
#include "../lang/lexer.hh"

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <rlx.hh>
#include <path/path.hh>
#include <boost/variant.hpp>

#define DEFINE_TYPE(type, method)                \
    {                                            \
#type, llvm::Type::get##method(*context) \
    }

namespace src::backend
{
    using namespace rlx;
    using namespace src::lang;

    class compiler
    {
    private:
        std::unique_ptr<llvm::LLVMContext> context;
        std::unique_ptr<llvm::IRBuilder<>> builder;
        std::map<std::string, llvm::AllocaInst *> values;
        std::map<std::string, llvm::Type *> types;
        std::map<std::string, std::vector<std::string>> structs;
        std::map<std::string, std::unique_ptr<ast::proto>> protos;
        llvm::TargetMachine *target_machine;

        std::vector<std::string> modules_loaded;
        std::string filename;

        void error(std::string const &x)
        {
            io::error(x);
            throw std::runtime_error("compilation failed");
        }

    public:
        std::unique_ptr<llvm::Module> module_;

        compiler(std::string f)
            : filename(f)
        {
            context = std::make_unique<llvm::LLVMContext>();
            builder = std::make_unique<llvm::IRBuilder<>>(*context);
            module_ = std::make_unique<llvm::Module>(filename, *context);

            types =
                {
                    DEFINE_TYPE(i8, Int8Ty),
                    DEFINE_TYPE(i16, Int16Ty),
                    DEFINE_TYPE(i32, Int32Ty),
                    DEFINE_TYPE(i64, Int64Ty),
                    DEFINE_TYPE(i128, Int128Ty),
                    DEFINE_TYPE(none, VoidTy)};
        }

        llvm::Value *operator()(ast::root_decls const &x)
        {
            for (auto const &i : x)
                boost::apply_visitor(*this, i);

            return nullptr;
        }

        void compile(std::string output = "a.o", std::string target_triple = "", std::string CPU = "generic", std::string features = "")
        {
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();

            if (target_triple.length() == 0)
                target_triple = llvm::sys::getDefaultTargetTriple();

            io::debug(level::debug, "Target: ", target_triple, "CPU: ", CPU);

            module_->setTargetTriple(target_triple);

            std::string err;
            auto target = llvm::TargetRegistry::lookupTarget(target_triple, err);
            if (!target)
                error(err);

            llvm::TargetOptions opt;
            auto rm = llvm::Optional<llvm::Reloc::Model>();

            target_machine =
                target->createTargetMachine(target_triple, CPU, features, opt, rm);

            module_->setDataLayout(target_machine->createDataLayout());

            std::error_code ec;
            llvm::raw_fd_ostream dest(output, ec, llvm::sys::fs::OF_None);

            if (ec)
                error(ec.message());

            llvm::legacy::PassManager pass;
            auto ftype = llvm::CGFT_ObjectFile;

            if (target_machine->addPassesToEmitFile(pass, dest, nullptr, ftype))
                error("target machine can't emit a file of this type");

            pass.run(*module_);
            dest.flush();
        }

        llvm::AllocaInst *_alloc(llvm::Function *fn, std::string const &id, llvm::Type *t, llvm::Value *size = 0)
        {
            llvm::IRBuilder<> tmpb(&fn->getEntryBlock(),
                                   fn->getEntryBlock().begin());

            return tmpb.CreateAlloca(t);
        }

        llvm::Value *operator()(ast::nil)
        {
            return nullptr;
        }

        llvm::Value *operator()(unsigned int x)
        {
            return llvm::ConstantInt::get(*context, llvm::APInt(32, x, false));
        }

        llvm::Value *operator()(std::string const &x)
        {
            return builder->CreateGlobalStringPtr(x);
        }

        llvm::Value *operator()(ast::ident const &x)
        {
            auto v = values[x.id];
            if (!v)
                error("unknow variable " + x.id);

            return builder->CreateLoad(v, x.id.c_str());
        }

        llvm::Value *operator()(ast::index const &x)
        {
            auto fn = builder->GetInsertBlock()->getParent();

            auto cont = values[boost::get<ast::ident>(x.id).id];
            auto idx = boost::apply_visitor(*this, x.val);

            auto v = builder->CreateLoad(cont);
            if (!v->getType()->isArrayTy())
                error("restricted index access from non-array value");

            io::debug(level::debug, "index type: ", idx->getValueName()->first().str());
            auto arr_size = (*this)((unsigned int)llvm::dyn_cast<llvm::ArrayType>(v->getType())->getNumElements());

            /// bound condition check
            auto cond = builder->CreateICmpSGE(idx, arr_size);

            auto thenbb =
                     llvm::BasicBlock::Create(*context, "ifyes", fn),
                 contbb =
                     llvm::BasicBlock::Create(*context, "cond", fn);

            builder->CreateCondBr(cond, thenbb, contbb);
            builder->SetInsertPoint(thenbb);
            auto exception_handler_fn = module_->getFunction("outofindex");
            if (!exception_handler_fn)
                error("no exception handler defined for 'outofindex'");

            builder->CreateCall(exception_handler_fn, {arr_size, idx});

            builder->CreateBr(contbb);
            builder->SetInsertPoint(contbb);

            auto ptr = builder->CreateGEP(cont, {(*this)((unsigned int)0), idx});
            return builder->CreateLoad(ptr);
        }

        llvm::Value *operator()(ast::binary const &x)
        {
            auto lhs = boost::apply_visitor(*this, x.lhs);
            auto rhs = boost::apply_visitor(*this, x.rhs);

            if (!lhs || !rhs)
                return nullptr;

            switch (x.oper)
            {
            case '+':
                return builder->CreateAdd(lhs, rhs);
            case '-':
                return builder->CreateSub(lhs, rhs);
            case '*':
                return builder->CreateMul(lhs, rhs);
            case '/':
                return builder->CreateFDiv(lhs, rhs);
            case eq:
                return builder->CreateICmpEQ(lhs, rhs);
            case ne:
                return builder->CreateICmpNE(lhs, rhs);
            case le:
                return builder->CreateICmpSLE(lhs, rhs);
            case '<':
                return builder->CreateICmpSLT(lhs, rhs);
            case ge:
                return builder->CreateICmpSGE(lhs, rhs);
            case '>':
                return builder->CreateICmpSGT(lhs, rhs);
            case and_:
                return builder->CreateAnd(lhs, rhs);
            case or_:
                return builder->CreateOr(lhs, rhs);

            default:
                error(io::format("operator '", x.oper, "' not yet implemented"));
            }

            return nullptr;
        }

        llvm::Function *get_fn(std::string const &id)
        {
            if (auto *f = module_->getFunction(id))
                return f;

            auto fi = protos.find(id);
            if (fi != protos.end())
                return (*this)(*fi->second.get());

            return nullptr;
        }

        llvm::Value *operator()(ast::unary const &x)
        {
            auto v = boost::apply_visitor(*this, x.lhs);

            switch (x.oper)
            {
            case tokentype::not_:
                return builder->CreateNot(v);

            case '-':
                return builder->CreateNeg(v);
            }
            error("invalid operator " + x.oper);
            return nullptr;
        }

        llvm::Value *operator()(ast::assign const &x)
        {
            auto id = boost::get<ast::ident>(x.id).id;
            auto val = boost::apply_visitor(*this, x.val);

            auto var = values[id];
            if (!var)
                error("unknow variable " + id);

            builder->CreateStore(val, var);
            return val;
        }

        llvm::Value *operator()(ast::let const &x)
        {
            auto fn = builder->GetInsertBlock()->getParent();
            auto a = _alloc(fn, x.var.ident_.id, (*this)(x.var));

            if (x.var.size > 1)
            {
                if (x.val.which() != 0)
                {
                    auto arr = boost::get<ast::array>(x.val);
                    // if ((*this)((unsigned int)arr.size()) != a->getArraySize())
                    //     error("invalid array size");

                    for (int i = 0; i < arr.size(); i++)
                    {
                        auto ptr = builder->CreateGEP(a, {(*this)((unsigned int)0), (*this)((unsigned int)i)});
                        builder->CreateStore(boost::apply_visitor(*this, arr[i]), ptr);
                    }
                }
            }
            else
            {
                llvm::Value *val = boost::apply_visitor(*this, x.val);
                if (val)
                    builder->CreateStore(val, a);
            }
            values[x.var.ident_.id] = a;

            return nullptr;
        }

        llvm::Value *operator()(ast::call const &x)
        {
            auto id = boost::get<ast::ident>(x.id).id;

            auto fn = module_->getFunction(id);
            if (!fn)
                error("unknown method call");

            if (fn->arg_size() != x.args.size() && !fn->isVarArg())
                error("incorrect # args");

            std::vector<llvm::Value *> args_v;
            for (size_t i = 0, e = x.args.size(); i != e; ++i)
            {
                auto a = boost::apply_visitor(*this, x.args[i]);
                if (!a)
                    return nullptr;

                args_v.push_back(a);
            }

            return builder->CreateCall(fn, args_v);
        }

        llvm::Value *operator()(ast::use const &x)
        {
            auto readfile = [](std::string const &path) -> std::string
            {
                std::ifstream file(path);
                std::string input(
                    (std::istreambuf_iterator<char>(file)),
                    (std::istreambuf_iterator<char>()));

                return input;
            };

            std::string module_path = rlx::path::dirname(filename) + "/" + x.path + ".src";

            if (!std::filesystem::exists(module_path))
            {
                std::string src_path = "/lib/src:/usr/lib/src";
                auto env_path = getenv("SRC_PATH");
                if (env_path)
                    src_path = std::string(env_path) + ":" + src_path;

                std::stringstream ss(src_path);
                std::string tpath;

                bool found = false;

                while (std::getline(ss, tpath, ':'))
                {
                    if (std::filesystem::exists(tpath + "/" + module_path))
                    {
                        found = true;
                        module_path = tpath;
                        break;
                    }
                }

                if (!found)
                    error("failed to found module " + x.path);
            }

            if (std::find(modules_loaded.begin(), modules_loaded.end(), module_path) != modules_loaded.end())
            {
                io::debug(level::trace, "already loaded", x.path);
                return nullptr;
            }

            using iterator = std::string::const_iterator;

            auto input = readfile(module_path);
            auto lexer = src::lang::lexer<iterator>(input.begin(), input.end());
            auto parser = src::lang::parser<iterator>(lexer);
            auto ast = parser.parse();
            (*this)(ast);

            return nullptr;
        }

        llvm::Value *operator()(ast::struct_ const &x)
        {
            io::debug(level::trace, "compiling struct");
            std::vector<llvm::Type *> members;
            structs[x.id.id] = std::vector<std::string>();
            for (auto const &i : x.vars)
            {
                auto t = (*this)(i);
                members.push_back(t);
                structs[x.id.id].push_back(i.ident_.id);
            }

            auto s = llvm::StructType::create(*context, x.id.id.c_str());
            s->setBody(members);
            types[x.id.id] = s;

            return nullptr;
        }

        llvm::Function *operator()(ast::proto const &x)
        {
            std::vector<llvm::Type *> params_types;
            for (auto const &i : x.args)
                params_types.push_back((*this)(i));

            auto ft = llvm::FunctionType::get((*this)(x.type_), params_types, x.is_variadic);
            auto fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, x.id.id.c_str(), module_.get());

            unsigned idx = 0;
            for (auto &i : fn->args())
                i.setName(x.args[idx++].ident_.id);

            return fn;
        }

        llvm::Function *operator()(ast::fn const &x)
        {
            auto fn = module_->getFunction(x.proto_.id.id);
            if (!fn)
                fn = (*this)(x.proto_);

            if (!fn)
                return nullptr;

            if (!fn->empty())
                error("fn can't be redefined");

            auto bb = llvm::BasicBlock::Create(*context, "entry", fn);
            builder->SetInsertPoint(bb);

            values.clear();
            for (auto &i : fn->args())
            {
                auto a = _alloc(fn, i.getName().str(), i.getType());
                values[i.getName().str()] = a;
            }

            (*this)(x.body);

            return fn;
        }

        llvm::Value *operator()(ast::block const &x)
        {
            for (auto const &i : x)
                boost::apply_visitor(*this, i);

            return nullptr;
        }

        llvm::Value *operator()(ast::ret const &x)
        {
            auto fn = builder->GetInsertBlock()->getParent();
            auto v = boost::apply_visitor(*this, x.val);
            if (v == nullptr && !fn->getReturnType()->isVoidTy())
                error("return type is not void");

            if (v == nullptr)
                return builder->CreateRetVoid();

            if (v->getType() != v->getType())
                error("return type is not same");

            return builder->CreateRet(v);
        }

        llvm::Value *operator()(ast::condition const &x)
        {
            auto cond = boost::apply_visitor(*this, x.cond);
            auto fn = builder->GetInsertBlock()->getParent();

            auto thenbb =
                     llvm::BasicBlock::Create(*context, "true", fn),
                 condbb =
                     llvm::BasicBlock::Create(*context, "cond", fn),
                 elsebb =
                     llvm::BasicBlock::Create(*context, "else", fn);

            builder->CreateCondBr(cond, thenbb, elsebb);

            builder->SetInsertPoint(thenbb);
            boost::apply_visitor(*this, x.then_);
            builder->CreateBr(condbb);

            builder->SetInsertPoint(elsebb);
            boost::apply_visitor(*this, x.else_);
            builder->CreateBr(condbb);

            builder->SetInsertPoint(condbb);

            return nullptr;
        }

        llvm::Value *operator()(ast::for_loop const &x)
        {
            auto fn = builder->GetInsertBlock()->getParent();
        }

        llvm::Value *operator()(ast::while_loop const &x)
        {
            auto fn = builder->GetInsertBlock()->getParent();
            auto condbb =
                     llvm::BasicBlock::Create(*context, "loop", fn),
                 bodybb =
                     llvm::BasicBlock::Create(*context, "body", fn),
                 contbb =
                     llvm::BasicBlock::Create(*context, "cont", fn);

            builder->CreateBr(condbb);
            builder->SetInsertPoint(condbb);

            auto condv = boost::apply_visitor(*this, x.cond);
            builder->CreateCondBr(condv, bodybb, contbb);

            builder->SetInsertPoint(bodybb);
            boost::apply_visitor(*this, x.body);
            builder->CreateBr(condbb);

            builder->SetInsertPoint(contbb);

            return nullptr;
        }

        llvm::Value *operator()(ast::expr_stmt const &x)
        {
            boost::apply_visitor(*this, x.expr_);
            return nullptr;
        }

        llvm::Type *operator()(ast::variable const &t)
        {
            auto ty = (*this)(t.type_);
            for (auto i : t.args)
            {
                switch (i)
                {
                case tokentype::ptr:
                    ty = llvm::PointerType::get(ty, 0);
                }
            }

            if (t.size > 1)
                ty = llvm::ArrayType::get(ty, t.size);

            return ty;
        }

        llvm::Type *operator()(ast::datatype const &t)
        {
            auto i = types.find(t.value.id);
            if (i == types.end())
                error("invalid datatype " + t.value.id);

            return i->second;
        }

        template <typename T>
        llvm::Value *operator()(T)
        {
            error(io::format("compiler not yet implemented for '", typeid(T).name(), "'"));
            return nullptr;
        }
    };
}

#endif
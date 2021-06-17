#ifndef __COMPILER__
#define __COMPILER__

#include "../lang/ast.hh"
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <rlx.hh>

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
        std::map<std::string, std::unique_ptr<ast::proto>> protos;

        void error(std::string const &x)
        {
            io::error(x);
            throw std::runtime_error("compilation failed");
        }

    public:
        std::unique_ptr<llvm::Module> module_;

        compiler()
        {
            context = std::make_unique<llvm::LLVMContext>();
            builder = std::make_unique<llvm::IRBuilder<>>(*context);
            module_ = std::make_unique<llvm::Module>("src", *context);

            types =
                {
                    DEFINE_TYPE(i8, Int8Ty),
                    DEFINE_TYPE(i16, Int16Ty),
                    DEFINE_TYPE(i32, Int32Ty),
                    DEFINE_TYPE(i64, Int64Ty),
                    DEFINE_TYPE(i128, Int128Ty),
                };
        }

        void compile(ast::root_decls const &x)
        {
            for (auto const &i : x)
                boost::apply_visitor(*this, i);
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

            std::string fn_id = "_src_unary_";
            switch (x.oper)
            {
            case tokentype::not_:
                fn_id += "not";
                break;

            case tokentype::ptr:
                return builder->CreateLoad(v);

            case tokentype::ref:
                fn_id += "ref";
                break;

            case '-':
                fn_id += "neg";
                break;
            }

            auto fn = get_fn(fn_id);
            if (!fn)
                error("unknown unary operator");

            return builder->CreateCall(fn, v, "unop");
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

        llvm::Value *operator()(ast::loop const &x)
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
#ifndef EvaLLVM_h
#define EvaLLVM_h

#include <memory>
#include <string>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include "EvaParser.h"

class EvaLLVM {

  public:
    EvaLLVM() {
        moduleInit();
        setupExternalFunctions();
    };

    ~EvaLLVM(){};

    /**
     * Execute the program
     */
    void eval(const std::string& program) {

        // 1. Parse the program
        auto ast = praser->parse(program);

        // 2. Generate LLVM IR
        compile(ast);

        // Verify the module for errors
        llvm::verifyModule(*module, &llvm::outs());

        // Print the generated IR
        module->print(llvm::outs(), nullptr);

        // 3. Save module IR to file:
        saveModuleToFile("./output.ll");
    }

  private:
    /**
     * Compile an expression
     */
    void compile(const Exp& ast) {
        // 1. Create a function
        fn = createFunction("main",
                            llvm::FunctionType::get(
                                /* result */ llvm::Type::getInt32Ty(*context),
                                /* vararg */ false));

        // 2. Compile main body
        gen(ast);

        // just return 0 for now
        builder->CreateRet(builder->getInt32(0));
    }

    /**
     * Create a function
     */
    llvm::Function* createFunction(const std::string& fnName,
                                   llvm::FunctionType* fnType) {
        // first try to get function
        auto fn = module->getFunction(fnName);
        if (fn == nullptr) {
            fn = createFunctionProto(fnName, fnType);
        }
        // use getOrInsertFunction to avoid redefinition
        createFunctionBlock(fn);
        return fn;
    }

    /**
     * Create a function prototype
     */
    llvm::Function* createFunctionProto(const std::string& fnName,
                                        llvm::FunctionType* fnType) {
        auto fn = llvm::Function::Create(
            fnType, llvm::Function::ExternalLinkage, fnName, *module);
        llvm::verifyFunction(*fn);

        return fn;
    }

    /**
     * Create a function block
     */
    void createFunctionBlock(llvm::Function* fn) {
        auto entry = createBB("entry", fn);
        builder->SetInsertPoint(entry);
    }

    /**
     * createBB
     */
    llvm::BasicBlock* createBB(const std::string& name,
                               llvm::Function* fn = nullptr) {
        return llvm::BasicBlock::Create(*context, name, fn);
    }

    /**
     * Main compile loop
     */
    llvm::Value* gen(const Exp& exp) {
        printf("Generating code for: %d\n", (int)exp.type);

        switch (exp.type) {

        case ExpType::NUMBER:
            return builder->getInt32(exp.number);

        case ExpType::STRING: {
            // handle \\n
            auto re = std::regex("\\\\n");
            auto str = std::regex_replace(exp.string, re, "\n");
            return builder->CreateGlobalStringPtr(str);
        }

        case ExpType::SYMBOL:
            assert(false && "Symbol not implemented");
            return nullptr;

        case ExpType::LIST: {
            // print list
            // if (exp.list.empty()) {
            //    // print error
            //    printf("Empty list\n");
            //    return builder->getInt32(0);
            //}

            if (!exp.list.empty()) {
                auto tag = exp.list[0];
                if (tag.type == ExpType::SYMBOL) {
                    if (tag.string == "printf") {
                        printf("Found printf\n");

                        const auto printfFn = module->getFunction("printf");
                        assert(printfFn && "Function 'printf' not found");

                        std::vector<llvm::Value*> args;

                        for (size_t i = 1; i < exp.list.size(); i++) {
                            args.push_back(gen(exp.list[i]));
                        }

                        return builder->CreateCall(printfFn, args);
                    }
                }
            } else {
                // print error
                printf("Empty list\n");
            }
            break;
        }

        }

        return builder->getInt32(0);
    }

    /**
     * Setup external functions
     */
    void setupExternalFunctions() {
        // add printf declaration
        auto printfType = llvm::FunctionType::get(
            /* result */ builder->getInt32Ty(),
            /* format arg */ builder->getInt8PtrTy(),
            /* vararg */ true);
        module->getOrInsertFunction("printf", printfType);
    }

    /**
     * Save the IR to a file
     */
    void saveModuleToFile(const std::string& fileName) {
        std::error_code errorCode;
        llvm::raw_fd_ostream outLL(fileName, errorCode);
        module->print(outLL, nullptr);
    }

    /**
     * Initialize the LLVM module
     */
    void moduleInit() {
        context = std::make_unique<llvm::LLVMContext>();
        module = std::make_unique<llvm::Module>("EvaLLVM", *context);
        builder = std::make_unique<llvm::IRBuilder<>>(*context);
        praser = std::make_unique<syntax::EvaParser>();
    }

    /**
     * Global LLVM context
     * It owns and manages the core "global" data of LLVM's core infrastructure,
     * including the type and constant uniquing tables.
     */
    std::unique_ptr<llvm::LLVMContext> context;

    /**
     * A module represents a single translation unit of code.
     */
    std::unique_ptr<llvm::Module> module;

    /**
     * The LLVM IR builder
     */
    std::unique_ptr<llvm::IRBuilder<>> builder;

    /**
     * The current function
     */
    llvm::Function* fn;

    /**
     * The parser
     */
    std::unique_ptr<syntax::EvaParser> praser;
};

#endif // EvaLLVM_h

#ifndef EvaLLVM_h
#define EvaLLVM_h

#include "Environment.h"

#include <memory>
#include <string>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include "Environment.h"
#include "EvaParser.h"

using Env = std::shared_ptr<Environment>;

class EvaLLVM {

  public:
    EvaLLVM() {
        moduleInit();
        setupExternalFunctions();
        setupGlobalEnvironment();
    };

    ~EvaLLVM(){};

    /**
     * Execute the program
     */
    void eval(const std::string& program,
            const std::string& fileName = "./output.ll") {

        // 1. Parse the program
        auto ast = parser->parse("(begin " + program + ")");

        // 2. Generate LLVM IR
        compile(ast);

        // Verify the module for errors
        llvm::verifyModule(*module, &llvm::outs());

        // Print the generated IR
        module->print(llvm::outs(), nullptr);

        // 3. Save module IR to file:
        saveModuleToFile(fileName);
    }

  private:
    /**
     * Setup the global environment
     */
    void setupGlobalEnvironment() {
        // Create a global variable for the version
        const std::map<std::string, llvm::Value*> globalObject{
            {"VERSION", builder->getInt32(10)}};

        std::map<std::string, llvm::Value*> globalRecord{};

        for (const auto& [name, value] : globalObject) {
            globalRecord[name] =
                createGlobalVar(name, llvm::dyn_cast<llvm::Constant>(value));
        }

        globalEnv = std::make_shared<Environment>(globalRecord, nullptr);
    }

    /**
     * Compile an expression
     */
    void compile(const Exp& ast) {
        // 1. Create a function
        fn = createFunction("main",
                            llvm::FunctionType::get(
                                /* result */ llvm::Type::getInt32Ty(*context),
                                /* vararg */ false),
                            globalEnv);

        createGlobalVar("VERSION", builder->getInt32(10));

        // 2. Compile main body
        gen(ast, globalEnv);

        // just return 0 for now
        builder->CreateRet(builder->getInt32(0));
    }

    /**
     * Create a function
     */
    llvm::Function* createFunction(const std::string& fnName,
                                   llvm::FunctionType* fnType, Env env) {
        // first try to get function
        auto fn = module->getFunction(fnName);
        if (fn == nullptr) {
            fn = createFunctionProto(fnName, fnType, env);
        }
        // use getOrInsertFunction to avoid redefinition
        createFunctionBlock(fn);

        return fn;
    }

    /**
     * Create a function prototype
     */
    llvm::Function* createFunctionProto(const std::string& fnName,
                                        llvm::FunctionType* fnType, Env env) {
        auto fn = llvm::Function::Create(
            fnType, llvm::Function::ExternalLinkage, fnName, *module);
        llvm::verifyFunction(*fn);

        // Create a new environment for the function
        env->define(fnName, fn);

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
    llvm::Value* gen(const Exp& exp, Env env) {
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

        case ExpType::SYMBOL: {
            /**
             * Boolean
             */
            if (exp.string == "true") {
                return builder->getInt1(true);
            } else if (exp.string == "false") {
                return builder->getInt1(false);
            } else {
                // Variables:
                auto varName = exp.string;
                auto varValue = env->lookup(varName);
                printf("Found variable: %s\n", varName.c_str());

                // 1. Local variables:
                if (varValue) {
                    return builder->CreateLoad(varValue->getType(), varValue,
                                               varName.c_str());
                }
            }
            std::runtime_error("Symbol not implemented");
        }
        case ExpType::LIST: {
            // print list
            // if (exp.list.empty()) {
            //    // print error
            //    printf("Empty list\n");
            //    return builder->getInt32(0);
            //}

            if (!exp.list.empty()) {
                const auto tag = exp.list[0];
                if (tag.type == ExpType::SYMBOL) {

                    // ----------------------------------------------------
                    // printf extern function:
                    //
                    // (printf "value %d" 42)
                    //
                    //
                    if (tag.string == "printf") {
                        printf("Found printf\n");

                        const auto printfFn = module->getFunction("printf");
                        assert(printfFn && "Function 'printf' not found");

                        std::vector<llvm::Value*> args;

                        for (size_t i = 1; i < exp.list.size(); i++) {
                            args.push_back(gen(exp.list[i], env));
                        }

                        return builder->CreateCall(printfFn, args);
                    }

                    // ----------------------------------------------------
                    // Variable delcaration: (var x (+ y 10))
                    //
                    // Typed: (var (x number) 42)
                    //
                    // Note: locals are allocated on the stack
                    else if (tag.string == "var") {
                        printf("Found var\n");
                        auto varNameDecl = exp.list[1];
                        auto varInitDecl = exp.list[2];
                        auto varName = extractVarName(varNameDecl);

                        // initializer
                        auto init = gen(varInitDecl, env);

                        // type
                        auto varTy = extractVarType(varNameDecl, varInitDecl);

                        // variable
                        auto varBinding = allocVar(varName, varTy, env);

                        // set value
                        return builder->CreateStore(init, varBinding);

                    }
                    // ----------------------------------------------------
                    // Block:
                    // (begin <exp1> <exp2> ... <expN>)
                    //
                    else if (tag.string == "begin") {
                        printf("Found begin\n");
                        llvm::Value* last = nullptr;

                        // create new environment
                        auto record = std::map<std::string, llvm::Value*>{};
                        auto newEnv =
                            std::make_shared<Environment>(record, env);

                        for (size_t i = 1; i < exp.list.size(); i++) {
                            last = gen(exp.list[i], newEnv);
                        }
                        return last;
                    }
                    // ----------------------------------------------------
                    // set:
                    // (set x 42)
                    // (set (x string) "Hello, World!")
                    else if (tag.string == "set") {
                        printf("Found set\n");
                        auto varNameDecl = exp.list[1];
                        auto varInitDecl = exp.list[2];
                        auto varName = extractVarName(varNameDecl);

                        // initializer
                        auto init = gen(varInitDecl, env);

                        // type
                        auto varTy = extractVarType(varNameDecl, varInitDecl);

                        // variable
                        auto varBinding = env->lookup(varName);

                        // set value
                        return builder->CreateStore(init, varBinding);
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
     * Extract the variable name
     */
    std::string extractVarName(const Exp& varDecl) {
        if (varDecl.type == ExpType::SYMBOL) {
            return varDecl.string;
        } else if (varDecl.type == ExpType::LIST) {
            return varDecl.list[0].string;
        } else {
            std::runtime_error("Invalid variable declaration");
        }
        return "";
    }

    /**
     * Extract the variable type
     */
    llvm::Type* extractVarType(const Exp& varDecl, const Exp& varInit) {
        if (varDecl.type == ExpType::SYMBOL) {
            if (varInit.type == ExpType::NUMBER) {
                return builder->getInt32Ty();
            } else if (varInit.type == ExpType::STRING) {
                return builder->getPtrTy();
            } else {
                std::runtime_error("Invalid variable type");
            }
        } else if (varDecl.type == ExpType::LIST) {
            if (varDecl.list[1].string == "number") {
                return builder->getInt32Ty();
            } else if (varDecl.list[1].string == "string") {
                return builder->getPtrTy();
            } else {
                std::runtime_error("Invalid variable type");
            }
        } else {
            std::runtime_error("Invalid variable declaration");
        }
        return nullptr;
    }

    /**
     * Allocate a variable
     */
    llvm::Value* allocVar(const std::string& varName, llvm::Type* varTy,
                          Env env) {
        varsBuilder->SetInsertPoint(&fn->getEntryBlock());
        auto var = varsBuilder->CreateAlloca(varTy, nullptr, varName);
        env->define(varName, var);
        return var;
    }

    /**
     * Creates a global variable
     */
    llvm::GlobalVariable* createGlobalVar(const std::string& name,
                                          llvm::Constant* init) {

        module->getOrInsertGlobal(name, init->getType());
        auto var = module->getNamedGlobal(name);
        var->setAlignment(llvm::MaybeAlign(4));
        var->setConstant(false);
        var->setInitializer(init);
        return var;
    }

    /**
     * Setup external functions
     */
    void setupExternalFunctions() {
        // add printf declaration
        auto printfType = llvm::FunctionType::get(
            /* result */ builder->getInt32Ty(),
            /* format arg */ builder->getPtrTy(),
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
        varsBuilder = std::make_unique<llvm::IRBuilder<>>(*context);
        parser = std::make_unique<syntax::EvaParser>();
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
     * Special builder for the alloca instruction, it always points to the
     * beginning of the current function.
     */
    std::unique_ptr<llvm::IRBuilder<>> varsBuilder;

    /**
     * The current function
     */
    llvm::Function* fn;

    /**
     * The parser
     */
    std::unique_ptr<syntax::EvaParser> parser;

    /**
     * The global environment (symbol table)
     */
    Env globalEnv;
};

#endif // EvaLLVM_h

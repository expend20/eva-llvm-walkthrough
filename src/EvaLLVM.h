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

/**
 * Class information
 */
struct ClassInfo {
    llvm::StructType* classType;
    llvm::StructType* parent;
    std::map<std::string, llvm::Type*> fields;
    std::map<std::string, llvm::Function*> methods;
};

inline std::string exp_type2str(ExpType type) {
    switch (type) {
    case ExpType::NUMBER:
        return "NUMBER";
    case ExpType::STRING:
        return "STRING";
    case ExpType::SYMBOL:
        return "SYMBOL";
    case ExpType::LIST:
        return "LIST";
    }
    return "UNKNOWN";
}

inline std::string exp_list2str(const std::vector<Exp>& list) {
    std::string str = "( ";
    for (const auto& exp : list) {
        if (exp.type == ExpType::NUMBER) {
            str += std::to_string(exp.number);
        } else if (exp.type == ExpType::STRING) {
            str += "\"" + exp.string + "\"";
        } else if (exp.type == ExpType::SYMBOL) {
            str += exp.string;
        } else if (exp.type == ExpType::LIST) {
            str += exp_list2str(exp.list);
        }
        str += " ";
    }
    str += ")";
    return str;
}

template <typename T> inline std::string dumpValueToString(const T* V) {
    std::string str;
    llvm::raw_string_ostream rso(str);
    V->print(rso);
    return rso.str();
}

class EvaLLVM {

  public:
    EvaLLVM() {
        moduleInit();
        setupExternalFunctions();
        setupGlobalEnvironment();
        setupTargetTriple();
    };

    ~EvaLLVM(){};

    /**
     * Setup the target triple
     */
    void setupTargetTriple() {
        // TODO: use default target triple llvm::sys::getDefaultTargetTriple();
        module->setTargetTriple("x86_64-unknown-linux-gnu");
    }

    /**
     * Execute the program
     */
    void eval(const std::string& program,
              const std::string& fileName = "./output.ll") {

        // 1. Parse the program
        printf("\nGenerating %s...\n\n", fileName.c_str());
        auto ast = parser->parse("(begin " + program + ")");

        // 2. Generate LLVM IR
        compile(ast);

        // Verify the module for errors
        llvm::verifyModule(*module, &llvm::outs());

        // Print the generated IR
        printf("\nProgram (%s):\n%s\n", fileName.c_str(), program.c_str());
        printf("Generated IR start:\n\n");
        module->print(llvm::outs(), nullptr);
        printf("\nGenerated IR end\n\n");

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

        const auto expType = exp_type2str(exp.type);
        if (expType == "LIST") {
            const auto s = exp_list2str(exp.list);
            printf("gen LIST: %s\n", s.c_str());
        }

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
            }

            {
                // Variables:
                auto varName = exp.string;
                // cast to stack allocated value: AllocaInst
                auto varValue =
                    llvm::dyn_cast<llvm::AllocaInst>(env->lookup(varName));

                // 1. Local variables:
                if (varValue) {
                    return builder->CreateLoad(varValue->getAllocatedType(),
                                               varValue, varName.c_str());
                }
            }

            {
                // Call to a function:
                auto fn = module->getFunction(exp.string);
                if (fn) {
                    return builder->CreateCall(fn);
                }
            }
            std::runtime_error("Symbol not implemented");
        }
        case ExpType::LIST: {

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
                        auto varNameDecl = exp.list[1];
                        auto varInitDecl = exp.list[2];
                        auto varName = extractVarName(varNameDecl);
                        // special case for class properties
                        if (classType != nullptr) {
                            return builder->getInt32(0);
                        }
                        // Class instance creation
                        // (var p (new Point 1 2))
                        if (varInitDecl.type == ExpType::LIST &&
                            varInitDecl.list[0].string == "new") {
                            return createClassInstance(varInitDecl, env,
                                                       varName);
                        }

                        // initializer
                        auto init = gen(varInitDecl, env);

                        // type
                        auto varTy = extractVarType(varNameDecl, varInitDecl);

                        // variable
                        auto varBinding = allocVar(varName, varTy, env);

                        // set value
                        assert(varBinding && "Variable not found");
                        assert(varBinding->getAllocatedType() == varTy &&
                               "Type mismatch on 'var' element");
                        builder->CreateStore(init, varBinding);
                        return init;

                    }
                    // ----------------------------------------------------
                    // Block:
                    // (begin <exp1> <exp2> ... <expN>)
                    //
                    else if (tag.string == "begin") {
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
                    //
                    // Class property access:
                    // (set (prop self x) x)
                    else if (tag.string == "set") {

                        auto varNameDecl = exp.list[1];
                        auto varInitDecl = exp.list[2];
                        auto varName = extractVarName(varNameDecl);

                        // initializer
                        auto init = gen(varInitDecl, env);

                        // type
                        auto varTy = extractVarType(varNameDecl, varInitDecl);

                        // variable
                        auto varBinding = llvm::dyn_cast<llvm::AllocaInst>(
                            env->lookup(varName));

                        // set value
                        assert(varBinding && "Variable not found");
                        assert(varBinding->getAllocatedType() == varTy &&
                               "Type mismatch on 'set' element");
                        builder->CreateStore(init, varBinding);
                        return init;
                    } // ----------------------------------------------------
                    // Arithmetic operations:
                    // (+ 1 2)
                    // (- 1 2)
                    // (* 1 2)
                    // (/ 1 2)
                    else if (tag.string == "+") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateAdd(lhs, rhs);
                    } else if (tag.string == "-") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateSub(lhs, rhs);
                    } else if (tag.string == "*") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateMul(lhs, rhs);
                    } else if (tag.string == "/") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateSDiv(lhs, rhs);
                    }
                    // ----------------------------------------------------
                    // Comparison operations:
                    // (== 1 2)
                    // (!= 1 2)
                    // (< 1 2)
                    // (<= 1 2)
                    // (> 1 2)
                    // (>= 1 2)
                    else if (tag.string == "==") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpEQ(lhs, rhs);
                    } else if (tag.string == "!=") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpNE(lhs, rhs);
                    } else if (tag.string == "<") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpSLT(lhs, rhs);
                    } else if (tag.string == "<=") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpSLE(lhs, rhs);
                    } else if (tag.string == ">") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpSGT(lhs, rhs);
                    } else if (tag.string == ">=") {
                        auto lhs = gen(exp.list[1], env);
                        auto rhs = gen(exp.list[2], env);
                        return builder->CreateICmpSGE(lhs, rhs);
                    }

                    // ----------------------------------------------------
                    // If statement:
                    // (if (== x 42) (set x 100) (set x 200))
                    //
                    else if (tag.string == "if") {
                        auto cond = gen(exp.list[1], env);
                        auto thenBB = createBB("then", fn);
                        auto elseBB = createBB("else", fn);
                        auto mergeBB = createBB("ifcont", fn);
                        builder->CreateCondBr(cond, thenBB, elseBB);

                        // then
                        builder->SetInsertPoint(thenBB);
                        auto thenVal = gen(exp.list[2], env);
                        builder->CreateBr(mergeBB);
                        thenBB = builder->GetInsertBlock();

                        // else
                        builder->SetInsertPoint(elseBB);
                        auto elseVal = gen(exp.list[3], env);
                        builder->CreateBr(mergeBB);
                        elseBB = builder->GetInsertBlock();

                        // merge
                        builder->SetInsertPoint(mergeBB);

                        auto phi = builder->CreatePHI(thenVal->getType(), 2);
                        phi->addIncoming(thenVal, thenBB);
                        phi->addIncoming(elseVal, elseBB);
                        return phi;
                    }

                    // ----------------------------------------------------
                    // While loop
                    // (while (< x 10) (set x (+ x 1)))
                    //
                    else if (tag.string == "while") {
                        auto condBB = createBB("cond", fn);
                        auto loopBB = createBB("loop", fn);
                        auto afterBB = createBB("afterloop", fn);
                        builder->CreateBr(condBB);

                        builder->SetInsertPoint(condBB);
                        auto cond = gen(exp.list[1], env);
                        builder->CreateCondBr(cond, loopBB, afterBB);

                        builder->SetInsertPoint(loopBB);
                        auto body = gen(exp.list[2], env);
                        builder->CreateBr(condBB);

                        builder->SetInsertPoint(afterBB);

                        return nullptr;
                    }

                    // ----------------------------------------------------
                    // Function definition
                    // Untyped:
                    //   (def square (x) (* x x))
                    // Typed:
                    //   (def sum ((a number) (b number)) -> number (+ a b))
                    //
                    else if (tag.string == "def") {
                        auto fnName = exp.list[1];
                        if (classType != nullptr) {
                            fnName.string = classType->getName().str() + "_" +
                                            fnName.string;
                        }
                        auto fnParamsDecl = exp.list[2];

                        auto argTypes = getArgTypes(exp);
                        auto argNames = getArgNames(exp);
                        auto retType = getRetType(exp);

                        // store insertion point
                        auto currentBlock = builder->GetInsertBlock();
                        auto currentFn = fn;

                        fn = createFunction(
                            fnName.string,
                            llvm::FunctionType::get(retType, argTypes, false),
                            env);
                        Exp fnBody = exp.list[3];
                        if (exp.list.size() == 6) {
                            fnBody = exp.list[5];
                        }
                        auto fnEnv = std::make_shared<Environment>(
                            std::map<std::string, llvm::Value*>{}, env);
                        auto fnArgs = fn->arg_begin();
                        for (size_t i = 0; i < argNames.size(); i++) {
                            auto argName = argNames[i];
                            fnArgs[i].setName(argName);
                            // store the argument in the function environment
                            fnEnv->define(argName, &fnArgs[i]);
                            // initialize the argument
                            auto arg = allocVar(argName, argTypes[i], fnEnv);
                            builder->CreateStore(&fnArgs[i], arg);
                        }
                        auto ret = gen(fnBody, fnEnv);
                        builder->CreateRet(ret);

                        // restore insertion point
                        builder->SetInsertPoint(currentBlock);
                        fn = currentFn;

                        return fn;
                    }

                    // ----------------------------------------------------
                    // Class definition
                    // (class Point null
                    //   (begin
                    //     (var x 0)
                    //     (var y 0)
                    //
                    //     ...
                    //   )
                    // )
                    else if (tag.string == "class") {
                        createClass(exp, env);
                        return nullptr; // TODO: implement
                    }

                    // ----------------------------------------------------
                    // Function call
                    // (square 2)
                    // try to find the function in the environment
                    auto fn =
                        llvm::dyn_cast<llvm::Function>(env->lookup(tag.string));
                    if (fn) {
                        std::vector<llvm::Value*> args;
                        for (size_t i = 1; i < exp.list.size(); i++) {
                            args.push_back(gen(exp.list[i], env));
                        }
                        return builder->CreateCall(fn, args);
                    }
                }
                printf("Not handled LIST: %s\n",
                       exp_list2str(exp.list).c_str());
            } else {
                std::runtime_error("Empty list");
            }
            break;
        } // case LIST
        } // switch
        printf("Not handled %s\n", exp_type2str(exp.type).c_str());
        assert(false && "Not implemented");
    }

    /**
     * Create a class instance
     */
    llvm::Value* createClassInstance(const Exp& exp, Env env,
                                     std::string& varName) {
        auto className = exp.list[1].string;
        auto classType = getClassByName(className);
        if (classType == nullptr) {
            auto e = "Class not found: " + className;
            std::runtime_error(e.c_str());
        }
        auto classInfo = classMap_[className];
        auto instName = varName.empty() ? className + "_inst" : varName;

        // Stack example:
        // auto instance = builder->CreateAlloca(classType, nullptr, varName);

        // malloc example:
        auto instance = mallocInsance(classType, "GC_malloc");

        // call the constructor
        auto constructor = module->getFunction(className + "_constructor");
        if (constructor == nullptr) {
            auto e = "Constructor not found for class: " + className;
            std::runtime_error(e.c_str());
        }
        std::vector<llvm::Value*> args;
        args.push_back(instance);
        for (size_t i = 2; i < exp.list.size(); i++) {
            args.push_back(gen(exp.list[i], env));
        }
        builder->CreateCall(constructor, args);
        return instance;
    }

    /**
     * Malloc a class instance
     */
    llvm::Value* mallocInsance(llvm::StructType* classType,
                               const std::string& name) {
        auto instance = builder->CreateCall(
            module->getFunction("GC_malloc"),
            builder->getInt32(getTypeSize(classType)), name);
        return builder->CreateBitCast(instance, classType->getPointerTo());
    }

    /**
     * Get type size
     */
    size_t getTypeSize(llvm::Type* type) {
        return module->getDataLayout().getTypeAllocSize(type);
    }

    /**
     * Create a Class
     */
    void createClass(const Exp& exp, Env env) {
        // check size of the list
        if (exp.list.size() != 4) {
            std::runtime_error("Invalid class definition");
        }
        auto className = exp.list[1].string;
        auto classParent = exp.list[2].string;
        auto classBody = exp.list[3];
        // printf("Creating class %s\n", className.c_str());
        // printf("Parent class %s\n", classParent.c_str());

        auto parent =
            classParent == "null" ? nullptr : getClassByName(classParent);

        // current class
        classType = llvm::StructType::create(*context, className);

        if (parent != nullptr) {
            inheritClass(classType, className);
        } else {
            classMap_[className] = {/* class */ classType,
                                    /* parent */ parent,
                                    /* fields */ {},
                                    /* methods */ {}};
        }

        // Scan the class body, since the constructor can call methods
        buildClassInfo(classType, exp, env);

        // Compile the body
        gen(classBody, env);

        // Reset the class type
        classType = nullptr;
    }

    /**
     * Build class information
     */
    void buildClassInfo(llvm::StructType* classType, const Exp& exp, Env env) {
        auto className = exp.list[1].string;
        // check size of the list
        if (exp.list.size() != 4) {
            std::runtime_error("Invalid class definition");
        }
        auto classBody = exp.list[3];

        // first element must be a string "begin"
        if (classBody.list[0].string != "begin") {
            std::runtime_error("Invalid class body, missing 'begin' element");
        }
        // printf("Building class info, first element: %s\n",
        //        classBody.list[0].string.c_str());

        // Shallow scanning of the class body, we need to know all the fields,
        // methods and their types
        for (size_t i = 1; i < classBody.list.size(); i++) {
            auto& beginLE = classBody.list[i];

            // everything else must be a list
            if (beginLE.type != ExpType::LIST) {
                std::runtime_error("Invalid class body, expected list element");
            }
            // printf("Building class info, element: %s\n",
            //        exp_list2str(beginLE.list).c_str());
            const auto firstLE = beginLE.list[0].string;

            // if var, update a struct
            if (firstLE == "var") {
                auto& expName = beginLE.list[1];
                auto& expInit = beginLE.list[2];

                auto varNameDecl = extractVarName(expName);
                auto varType = extractVarType(expName, expInit);
                classMap_[className].fields[varNameDecl] = varType;
                // printf("Building class info, var: %s, init...\n",
                //        varNameDecl.c_str());
            }
            // if def, create a function
            else if (firstLE == "def") {
                auto fnName = beginLE.list[1];
                auto fnParamsDecl = beginLE.list[2];
                auto argTypes = getArgTypes(beginLE);
                auto argNames = getArgNames(beginLE);
                auto retType = getRetType(beginLE);
                // first argument is always self
                if (argNames[0] != "self") {
                    std::runtime_error("First argument must be 'self'");
                }
                classMap_[className].methods[exp.string] =
                    llvm::Function::Create(llvm::FunctionType::get(
                                               /* result */ retType,
                                               /* args */ argTypes, false),
                                           llvm::Function::ExternalLinkage,
                                           exp.string, *module);
                // printf("Building class info, method: %s\n",
                //        fnName.string.c_str());

            } else {
                // printf("Unknown class body element: %s\n", firstLE.c_str());
                std::runtime_error("Invalid class body element");
            }
        }
        // init struct fields
        std::vector<llvm::Type*> fields;
        for (const auto& [name, type] : classMap_[className].fields) {
            fields.push_back(type);
        }
        classType->setBody(fields);
        printf("Class info built: %s\n", dumpValueToString(classType).c_str());
    }

    /**
     * Inherit a class
     */
    void inheritClass(llvm::StructType* classType, const std::string& name) {
        // TODO: implement
    }

    /**
     * Get a class by name
     */
    llvm::StructType* getClassByName(const std::string& name) {
        return llvm::StructType::getTypeByName(*context, name);
    }

    /**
     * Get the return type
     */
    llvm::Type* getRetType(const Exp& exp) {
        if (exp.list.size() == 4) {
            return builder->getInt32Ty();
        } else if (exp.list.size() == 6) {
            auto possibleArrowStr = exp.list[4];
            if (possibleArrowStr.type == ExpType::SYMBOL &&
                possibleArrowStr.string == "->") {
                auto retType = exp.list[5];
                if (retType.string == "number") {
                    return builder->getInt32Ty();
                } else if (retType.string == "string") {
                    return builder->getPtrTy();
                } else {
                    std::runtime_error("Invalid return type");
                }
            }
        }
        printf("Unknown return type, assuming int\n");
        return builder->getInt32Ty();
    }

    /**
     * Get the argument types
     */
    std::vector<llvm::Type*> getArgTypes(const Exp& exp) {
        std::vector<llvm::Type*> argTypes;
        if (exp.list.size() > 2) {
            auto fnParamsDecl = exp.list[2];
            for (size_t i = 0; i < fnParamsDecl.list.size(); i++) {
                auto argDecl = fnParamsDecl.list[i];
                if (argDecl.type == ExpType::LIST) {
                    if (argDecl.list.size() == 2) {
                        auto argType = argDecl.list[1].string;
                        if (argType == "number") {
                            argTypes.push_back(builder->getInt32Ty());
                        } else if (argType == "string") {
                            argTypes.push_back(builder->getPtrTy());
                        } else {
                            std::runtime_error("Invalid argument type");
                        }
                    } else {
                        std::runtime_error("Invalid argument declaration");
                    }
                } else if (argDecl.type == ExpType::SYMBOL) {
                    // check if the name is 'self'
                    if (argDecl.string == "self") {
                        // printf("Found 'self' argument\n");
                        // classType->dump();
                        argTypes.push_back(classType->getPointerTo());
                    } else {
                        // assume int if untyped
                        argTypes.push_back(builder->getInt32Ty());
                    }
                } else {
                    std::runtime_error("Invalid argument declaration");
                }
            }
        }
        return argTypes;
    }

    /**
     * Get the argument names
     */
    std::vector<std::string> getArgNames(const Exp& exp) {
        std::vector<std::string> argNames;
        if (exp.list.size() > 2) {
            auto fnParamsDecl = exp.list[2];
            for (size_t i = 0; i < fnParamsDecl.list.size(); i++) {
                auto argDecl = fnParamsDecl.list[i];
                if (argDecl.type == ExpType::LIST) {
                    auto argName = argDecl.list[0].string;
                    argNames.push_back(argName);
                } else if (argDecl.type == ExpType::SYMBOL) {
                    argNames.push_back(argDecl.string);
                } else {
                    std::runtime_error("Invalid argument declaration");
                }
            }
        }
        return argNames;
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
        printf("Unknown variable type for '%s', assuming int\n",
               varDecl.string.c_str());
        return builder->getInt32Ty();
    }

    /**
     * Allocate a variable
     */
    llvm::AllocaInst* allocVar(const std::string& varName, llvm::Type* varTy,
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

        // add malloc declaration
        auto mallocType = llvm::FunctionType::get(
            /* result */ builder->getPtrTy(),
            /* size_t arg */ builder->getInt32Ty(),
            /* vararg */ false);
        module->getOrInsertFunction("GC_malloc", mallocType);
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
     * It owns and manages the core "global" data of LLVM's core
     * infrastructure, including the type and constant uniquing tables.
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
    llvm::Function* fn = nullptr;

    /**
     * The parser
     */
    std::unique_ptr<syntax::EvaParser> parser;

    /**
     * The global environment (symbol table)
     */
    Env globalEnv;

    /**
     * The class type
     */
    llvm::StructType* classType = nullptr;

    /**
     * The class map
     */
    std::map<std::string, ClassInfo> classMap_;
};

#endif // EvaLLVM_h

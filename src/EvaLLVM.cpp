#include "EvaLLVM.h"

#include "Environment.h"
#include "EvaParser.h"

#include <cstdarg>
#include <llvm/IR/Verifier.h>

// dprintf is printf for debug messages, it's enabled with EVA_DEBUG env var
void dprintf(const char* fmt, ...) {
    static bool debug = std::getenv("EVA_DEBUG");
    if (!debug) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

std::string exp_type2str(ExpType type) {
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

std::string exp_non_list2str(const Exp& exp) {
    if (exp.type == ExpType::NUMBER) {
        return std::to_string(exp.number);
    } else if (exp.type == ExpType::STRING) {
        return "\"" + exp.string + "\"";
    } else if (exp.type == ExpType::SYMBOL) {
        return exp.string;
    }
    return "UNKNOWN";
}

std::string exp_list2str(const Exp& exp) {
    std::string str = "( ";
    for (const auto& e : exp.list) {
        if (e.type != ExpType::LIST) {
            str += exp_non_list2str(e);
        } else if (e.type == ExpType::LIST) {
            str += exp_list2str(e);
        }
        str += " ";
    }
    str += ")";
    return str;
}

// TODO: merge with exp_list2str
std::string exp2str(const Exp& exp) {
    if (exp.type != ExpType::LIST) {
        return exp_non_list2str(exp);
    } else {
        return exp_list2str(exp);
    }
}

template <typename T> std::string dumpValueToString(const T* V) {
    if (V == nullptr) {
        return "nullptr";
    }
    std::string              str;
    llvm::raw_string_ostream rso(str);
    V->print(rso);
    return rso.str();
}

/**
 * Setup the target triple
 */
void EvaLLVM::setupTargetTriple() {
    // TODO: use default target triple llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple("x86_64-unknown-linux-gnu");
}

/**
 * Execute the program
 */
void EvaLLVM::eval(const std::string& program, const std::string& fileName) {

    // 1. Parse the program
    printf("\nGenerating %s...\n\n", fileName.c_str());
    auto ast = parser->parse("(begin " + program + ")");

    // 2. Generate LLVM IR
    compile(ast);

    // Verify the module for errors
    llvm::verifyModule(*module, &llvm::outs());

    // Print the generated IR if "EVA_COUT" env is set
    if (std::getenv("EVA_COUT")) {
        printf("\nProgram (%s):\n%s\n", fileName.c_str(), program.c_str());
        printf("Generated IR start:\n\n");
        module->print(llvm::outs(), nullptr);
        printf("\nGenerated IR end\n\n");
    }

    // 3. Save module IR to file:
    saveModuleToFile(fileName);
}

/**
 * Setup the global environment
 */
void EvaLLVM::setupGlobalEnvironment() {
    // Create a global variable for the version
    std::map<std::string, ValueType> globalObject;
    globalObject["VERSION"] = Environment::make_value(builder->getInt32(10));

    std::map<std::string, ValueType> globalRecord{};

    for (const auto& [name, value] : globalObject) {
        globalRecord[name] = {
            createGlobalVar(name, llvm::dyn_cast<llvm::Constant>(value.value)),
            nullptr};
    }

    globalEnv = std::make_shared<Environment>(globalRecord, nullptr);
}

/**
 * Compile an expression
 */
void EvaLLVM::compile(const Exp& ast) {
    // 1. Create a function
    fn = createFunction(
        "main",
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
llvm::Function* EvaLLVM::createFunction(
    const std::string& fnName, llvm::FunctionType* fnType, Env env) {
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
llvm::Function* EvaLLVM::createFunctionProto(
    const std::string& fnName, llvm::FunctionType* fnType, Env env) {
    auto fn = llvm::Function::Create(
        fnType, llvm::Function::ExternalLinkage, fnName, *module);
    llvm::verifyFunction(*fn);

    // Create a new environment for the function
    env->define(fnName, fn, fn->getType());

    return fn;
}

/**
 * Create a function block
 */
void EvaLLVM::createFunctionBlock(llvm::Function* fn) {
    auto entry = createBB("entry", fn);
    builder->SetInsertPoint(entry);
}

/**
 * createBB
 */
llvm::BasicBlock*
EvaLLVM::createBB(const std::string& name, llvm::Function* fn) {
    return llvm::BasicBlock::Create(*context, name, fn);
}

/**
 * Main compile loop
 */
ValueType EvaLLVM::gen(const Exp& exp, Env env) {

    static size_t spaces = 0;

    ValueType   result{nullptr, nullptr};
    std::string indent(spaces, ' ');
    dprintf("%sgen: %s\n", indent.c_str(), exp2str(exp).c_str());
    spaces += 2;

    switch (exp.type) {

    case ExpType::NUMBER:
        result = {builder->getInt32(exp.number), nullptr};
        break;

    case ExpType::STRING: {
        // handle \\n
        auto re = std::regex("\\\\n");
        auto str = std::regex_replace(exp.string, re, "\n");
        result = {builder->CreateGlobalStringPtr(str), builder->getInt8Ty()};
        break;
    }

    case ExpType::SYMBOL: {
        /**
         * Boolean
         */
        if (exp.string == "true") {
            result = {builder->getInt1(true), nullptr};
            break;
        } else if (exp.string == "false") {
            result = {builder->getInt1(false), nullptr};
        }

        // printf("Calling a function: %s\n", exp.string.c_str());
        auto fn = module->getFunction(exp.string);
        if (fn != nullptr) {
            dprintf(
                "%sFunction found: %s\n", indent.c_str(), exp.string.c_str());
            result = {builder->CreateCall(fn), nullptr};
            break;
        }

        // Variables:
        // printf("Looking up variable: %s\n", exp.string.c_str());
        auto varName = exp.string;
        // cast to stack allocated value: AllocaInst
        auto var = env->lookup(varName);
        auto varAlloca = llvm::dyn_cast<llvm::AllocaInst>(var.value);
        // 1. Local variables:
        if (varAlloca != nullptr) {
            dprintf(
                "%sVariable found (AllocaInst): %s\n",
                indent.c_str(),
                varName.c_str());
            result = {
                builder->CreateLoad(
                    varAlloca->getAllocatedType(), varAlloca, varName.c_str()),
                var.type};
            break;
        }

        // last resort: variables
        if (var.value) {
            dprintf(
                "%sVariable found: %s, orig type %s\n",
                indent.c_str(),
                varName.c_str(),
                dumpValueToString(var.type).c_str());
            result = var;
            break;
        }

        throw std::runtime_error("Symbol not implemented");
    }
    case ExpType::LIST: {
        if (exp.list.empty()) {
            throw std::runtime_error("Empty list");
        }

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
                    args.push_back(gen(exp.list[i], env).value);
                }

                result = {builder->CreateCall(printfFn, args), nullptr};
                break;
            }

            // ----------------------------------------------------
            // Variable delcaration: (var x (+ y 10))
            //
            // Typed: (var (x number) 42)
            // Derived type: ( var x ( prop self cell ) ) // Cell type
            //
            // Note: locals are allocated on the stack
            else if (tag.string == "var") {
                auto varNameDecl = exp.list[1];
                auto varInitDecl = exp.list[2];
                auto varName = extractVarName(varNameDecl);
                dprintf(
                    "%sVariable declaration: %s\n",
                    indent.c_str(),
                    varName.c_str());

                // Class instance creation
                // (var p (new Point 1 2))
                if (varInitDecl.type == ExpType::LIST &&
                    varInitDecl.list[0].string == "new") {
                    result = {
                        createClassInstance(varInitDecl, env, varName),
                        classMap_[varInitDecl.list[0].string].classType};
                    break;
                }

                // initializer
                auto genValueType = gen(varInitDecl, env);
                dprintf(
                    "%sgen result: %s\n",
                    indent.c_str(),
                    dumpValueToString(genValueType.value).c_str());
                dprintf(
                    "%sgen type ptr: %s\n",
                    indent.c_str(),
                    dumpValueToString(genValueType.type).c_str());

                // variable
                auto varBinding =
                    allocVar(varName, genValueType.value->getType(), env);
                const auto definedType = genValueType.type == nullptr
                    ? genValueType.value->getType()
                    : genValueType.type;
                env->define(varName, varBinding, definedType);
                dprintf(
                    "%sVariable binding: %s\n",
                    indent.c_str(),
                    dumpValueToString(varBinding).c_str());

                // set value
                assert(varBinding && "Variable not found");
                builder->CreateStore(genValueType.value, varBinding);
                result = {varBinding, genValueType.type};
                break;

            }
            // ----------------------------------------------------
            // Block:
            // (begin <exp1> <exp2> ... <expN>)
            //
            else if (tag.string == "begin") {

                // create new environment
                auto record = std::map<std::string, ValueType>{};
                auto newEnv = std::make_shared<Environment>(record, env);

                for (size_t i = 1; i < exp.list.size(); i++) {
                    result = gen(exp.list[i], newEnv);
                }
                break;
            }
            // ----------------------------------------------------
            // set:
            // (set x 42)
            // (set (x string) "Hello, World!")
            //
            // Class property access:
            // (set (prop self Point x) x)
            else if (tag.string == "set") {

                // if it's a property access, call the setter
                if (exp.list[1].type == ExpType::LIST &&
                    exp.list[1].list[0].string == "prop") {
                    auto newValue = gen(exp.list[2], env);
                    result = {
                        accessProperty(exp.list[1], env, newValue.value).value,
                        newValue.type};
                    break;
                }

                auto varNameDecl = exp.list[1];
                auto varInitDecl = exp.list[2];
                auto varName = extractVarName(varNameDecl);

                // initializer
                auto genValue = gen(varInitDecl, env);

                // type
                // auto varTy = extractVarType(varNameDecl);

                // variable
                const auto varInit = env->lookup(varName);
                dprintf(
                    "%sVariable found: %s\n",
                    indent.c_str(),
                    dumpValueToString(varInit.value).c_str());
                // auto varBinding =
                //     llvm::dyn_cast<llvm::AllocaInst>(varInit.value);

                // set value
                assert(varInit.value && "Variable not found");
                builder->CreateStore(genValue.value, varInit.value);
                result = {genValue.value, genValue.type};
                break;
            } // ----------------------------------------------------
            // Arithmetic operations:
            // (+ 1 2)
            // (- 1 2)
            // (* 1 2)
            // (/ 1 2)
            else if (tag.string == "+") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {builder->CreateAdd(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "-") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {builder->CreateSub(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "*") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {builder->CreateMul(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "/") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {builder->CreateSDiv(lhs.value, rhs.value), lhs.type};
                break;
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
                result = {
                    builder->CreateICmpEQ(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "!=") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {
                    builder->CreateICmpNE(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "<") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {
                    builder->CreateICmpSLT(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == "<=") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {
                    builder->CreateICmpSLE(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == ">") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {
                    builder->CreateICmpSGT(lhs.value, rhs.value), lhs.type};
                break;
            } else if (tag.string == ">=") {
                auto lhs = gen(exp.list[1], env);
                auto rhs = gen(exp.list[2], env);
                result = {
                    builder->CreateICmpSGE(lhs.value, rhs.value), lhs.type};
                break;
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
                builder->CreateCondBr(cond.value, thenBB, elseBB);

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

                auto phi = builder->CreatePHI(thenVal.value->getType(), 2);
                phi->addIncoming(thenVal.value, thenBB);
                phi->addIncoming(elseVal.value, elseBB);
                // result = {builder->getInt32(0), nullptr};
                result = {phi, nullptr};
                break;
            }

            // ----------------------------------------------------
            // While loop
            // (while (< x 10) (set x (+ x 1)))
            //
            else if (tag.string == "while") {
                dprintf("%sWhile loop\n", indent.c_str());
                auto condBB = createBB("cond", fn);
                auto loopBB = createBB("loop", fn);
                auto afterBB = createBB("afterloop", fn);
                builder->CreateBr(condBB);

                builder->SetInsertPoint(condBB);
                auto cond = gen(exp.list[1], env);
                builder->CreateCondBr(cond.value, loopBB, afterBB);

                builder->SetInsertPoint(loopBB);
                auto body = gen(exp.list[2], env);
                builder->CreateBr(condBB);

                builder->SetInsertPoint(afterBB);

                dprintf("%sWhile loop end\n", indent.c_str());
                result = {builder->getInt32(0), nullptr};
                break;
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
                    fnName.string =
                        classType->getName().str() + "_" + fnName.string;
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
                    std::map<std::string, ValueType>{}, env);
                auto fnArgs = fn->arg_begin();
                for (size_t i = 0; i < argNames.size(); i++) {
                    auto argName = argNames[i];
                    dprintf(
                        "%sParsing args: %s, class %s\n",
                        indent.c_str(),
                        argName.c_str(),
                        dumpValueToString(argTypes[i]).c_str());
                    fnArgs[i].setName(argName);
                    // store the argument in the function environment
                    // TODO: arg param can be another class
                    fnEnv->define(argName, &fnArgs[i], classType);
                    // initialize the argument
                    auto arg = allocVar(argName, argTypes[i], fnEnv);
                    builder->CreateStore(&fnArgs[i], arg);
                }
                auto ret = gen(fnBody, fnEnv);
                builder->CreateRet(ret.value);

                auto typeStr = dumpValueToString(fn->getFunctionType());
                dprintf(
                    "%sFunction defined: %s %s\n",
                    indent.c_str(),
                    fnName.string.c_str(),
                    typeStr.c_str());
                // restore insertion point
                builder->SetInsertPoint(currentBlock);
                fn = currentFn;

                result = {fn, nullptr};
                break;
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
                result = {builder->getInt32(0), nullptr};
                break;
            }

            // ----------------------------------------------------
            // Property access getter
            // (prop Point p x)
            else if (tag.string == "prop") {
                result = accessProperty(exp, env);
                break;
            }

            // ----------------------------------------------------
            // Method / super call
            // (method p calc)
            // (method (self Point) calc)
            else if (tag.string == "method") {
                std::string instName;
                std::string specifiedType;
                if (exp.list[1].type == ExpType::SYMBOL) {
                    instName = exp.list[1].string;
                } else {
                    instName = exp.list[1].list[0].string;
                    specifiedType = exp.list[1].list[1].string;
                }
                auto methodName = exp.list[2].string;
                auto inst = gen(instName, env);
                // original class name
                auto className = inst.type->getStructName().str();
                dprintf(
                    "%sClass name: %s\n", indent.c_str(), className.c_str());
                if (!specifiedType.empty()) {
                    className = specifiedType;
                }
                dprintf(
                    "%sSpecified class name: %s\n",
                    indent.c_str(),
                    className.c_str());

                auto         funcName = className + "_" + methodName;
                auto         classInfo = classMap_[className];
                llvm::Value* fnDest = nullptr;
                // we're only using vtable if outside of a class, we must use
                // direct function call inside of a class
                if (classType == nullptr) {
                    dprintf(
                        "%sMethod call outside of class: %s.%s\n",
                        indent.c_str(),
                        className.c_str(),
                        methodName.c_str());
                    fnDest = loadVtablePtr(inst.value, methodName, className);
                } else {
                    fnDest = module->getFunction(funcName);
                }

                if (fnDest == nullptr) {
                    auto e = "Method not found: " + funcName;
                    throw std::runtime_error(e.c_str());
                }
                dprintf(
                    "%sCalling method: %s\n", indent.c_str(), funcName.c_str());

                result = {
                    builder->CreateCall(
                        classInfo.methodTypes[methodName]->getFunctionType(),
                        fnDest,
                        genMethodArgs(inst.value, exp, 3, env)),
                    nullptr};
                break;
            }

            // ----------------------------------------------------
            // Function call
            // (square 2)
            // try to find the function in the environment
            dprintf(
                "%sLooking up function: %s\n",
                indent.c_str(),
                tag.string.c_str());
            // auto fn =
            //     llvm::dyn_cast<llvm::Function>(env->lookup(tag.string));
            auto fn = module->getFunction(tag.string);
            if (fn) {
                dprintf(
                    "%sFunction found: %s\n",
                    indent.c_str(),
                    tag.string.c_str());
                result = {
                    builder->CreateCall(fn, genFunctionArgs(exp, 1, env)),
                    nullptr};
                break;
            }
            dprintf(
                "%sFunction not found: %s\n",
                indent.c_str(),
                tag.string.c_str());

            // ----------------------------------------------------
            // Functor call
            // (transform 10) // where transform is a class with __call__ method
            auto callable = getCallable(exp, env);
            if (callable != nullptr) {
                dprintf(
                    "%sCalling a functor/callable: %s\n",
                    indent.c_str(),
                    tag.string.c_str());
                const auto classInfo = getClassInfoByVarName(tag.string, env);
                const auto fnDest = loadVtablePtr(
                    callable,
                    "__call__",
                    classInfo->classType->getStructName().str());
                result = {
                    builder->CreateCall(
                        classInfo->methodTypes["__call__"]->getFunctionType(),
                        fnDest,
                        genMethodArgs(callable, exp, 1, env)),
                    nullptr};
                break;
            }
            dprintf(
                "%sCallable not found: %s\n",
                indent.c_str(),
                tag.string.c_str());
        }
        break;
    } // case LIST
    } // switch

    if (result.value == nullptr) {
        printf(
            "%sNot handled %s: %s\n",
            indent.c_str(),
            exp_type2str(exp.type).c_str(),
            exp2str(exp).c_str());
        throw std::runtime_error("Not implemented");
    }
    spaces -= 2;
    dprintf(
        "%sgen result: value %s, type %s\n",
        indent.c_str(),
        dumpValueToString(result.value).c_str(),
        dumpValueToString(result.type).c_str());
    return result;
}

/**
 * Gen arguments
 */
std::vector<llvm::Value*>
EvaLLVM::genFunctionArgs(const Exp& exp, size_t start, Env env) {
    std::vector<llvm::Value*> args;
    for (size_t i = start; i < exp.list.size(); i++) {
        args.push_back(gen(exp.list[i], env).value);
    }
    return args;
}

/**
 * Get method arguments
 */
std::vector<llvm::Value*> EvaLLVM::genMethodArgs(
    llvm::Value* inst, const Exp& exp, size_t start, Env env) {
    std::vector<llvm::Value*> args;
    args.push_back(inst);
    for (size_t i = start; i < exp.list.size(); i++) {
        args.push_back(gen(exp.list[i], env).value);
    }
    return args;
}

/**
 * Get callable
 */
llvm::Value* EvaLLVM::getCallable(const Exp& exp, Env env) {
    const auto tag = exp.list[0];
    const auto classInfo = getClassInfoByVarName(tag.string, env);
    if (classInfo == nullptr) {
        return nullptr;
    }
    const auto className = classInfo->classType->getStructName().str();

    return gen(tag, env).value;
}

/**
 * Get ClassInfo* by variable name
 */
ClassInfo* EvaLLVM::getClassInfoByVarName(const std::string& varName, Env env) {
    auto type = env->lookup(varName).type;
    if (type == nullptr) {
        return nullptr;
    }
    auto className = type->getStructName().str();
    if (classMap_.find(className) == classMap_.end()) {
        return nullptr;
    }
    return &classMap_[className];
}

/**
 * Build vtable call
 */
llvm::Value* EvaLLVM::loadVtablePtr(
    llvm::Value*       inst,
    const std::string& methodName,
    const std::string& className) {
    // get fn from vtable
    auto idx = getMethodIndex(className, methodName);
    auto vtableGlobalVar = module->getGlobalVariable(className + "_vtable_var");
    auto vtableType = vtableGlobalVar->getValueType();
    // fetch vtable pointer from the instance
    auto classInfo = classMap_[className];
    auto vtablePtr =
        builder->CreateStructGEP(classInfo.classType, inst, 0, "vtable_gep");
    auto vtable =
        builder->CreateLoad(vtableType->getPointerTo(), vtablePtr, "vtable");
    // fetch the method pointer from the vtable
    auto fnPtr = builder->CreateStructGEP(vtableType, vtable, idx, "method");
    return builder->CreateLoad(
        classInfo.methodTypes[methodName]->getType()->getPointerTo(),
        fnPtr,
        "method");
}

/**
 * Access a property
 * If newValue is provided it's a setter
 * If newValue is nullptr it's a getter
 */
ValueType
EvaLLVM::accessProperty(const Exp& exp, Env env, llvm::Value* newValue) {
    dprintf("Accessing property: %s\n", exp2str(exp).c_str());
    auto instExp = exp.list[1];
    auto varName = exp.list[2].string;
    auto genValue = gen(instExp, env);
    dprintf("Accessing property instExp: %s\n", exp2str(instExp).c_str());
    auto type = genValue.type;
    auto className = type->getStructName().str();
    auto classInfo = classMap_[className];
    if (type == nullptr) {
        auto e = "Class not found: " + varName;
        throw std::runtime_error(e.c_str());
    }
    const auto structIdx = getFieldIndex(type, varName);
    if (newValue != nullptr) { // setter
        auto propPtr = builder->CreateStructGEP(
            type, genValue.value, structIdx, "propPtr" + varName);
        builder->CreateStore(newValue, propPtr, "prop");
        return {builder->getInt32(0), nullptr};
    } else { // getter
        auto propPtr = builder->CreateStructGEP(
            type, genValue.value, structIdx, "propPtr" + varName);

        dprintf(
            "genValue valuetype: %s, is pointer %d, type %s\n",
            dumpValueToString(genValue.value).c_str(),
            genValue.value->getType()->isPointerTy(),
            dumpValueToString(genValue.type).c_str());
        return {
            builder->CreateLoad(
                classInfo.fieldTypes[varName].type, propPtr, "prop"),
            classInfo.fieldTypes[varName].ptrType};
    }
}

/**
 * Get the struct index
 */
size_t EvaLLVM::getFieldIndex(llvm::Type* type, const std::string& field) {
    auto structName = type->getStructName().str();
    dprintf("Getting index for %s.%s\n", structName.c_str(), field.c_str());
    auto   classInfo = classMap_[structName];
    size_t idx = 1; // first element is the vtable
    for (const auto& f : classInfo.fieldNames) {
        if (f == field) {
            dprintf(
                "Field found: %s.%s at index %lu\n",
                structName.c_str(),
                field.c_str(),
                idx);
            return idx;
        }
        idx++;
    }
    auto s = "Field not found: " + structName + "." + field;
    throw std::runtime_error(s.c_str());
}

size_t EvaLLVM::getMethodIndex(
    const std::string& structName, const std::string& field) {
    // auto structName = type->getStructName().str();
    dprintf("Getting index for %s.%s\n", structName.c_str(), field.c_str());
    auto   classInfo = classMap_[structName];
    size_t idx = 0;
    for (const auto& f : classInfo.methodNames) {
        if (f == field) {
            dprintf(
                "Method found: %s.%s at index %lu\n",
                structName.c_str(),
                field.c_str(),
                idx);
            return idx;
        }
        idx++;
    }
    auto s = "Method not found: " + structName + "." + field;
    throw std::runtime_error(s.c_str());
}

/**
 * Create a class instance
 */
llvm::Value*
EvaLLVM::createClassInstance(const Exp& exp, Env env, std::string& varName) {
    auto className = exp.list[1].string;
    auto classType = getClassByName(className);
    if (classType == nullptr) {
        auto e = "Class not found: " + className;
        throw std::runtime_error(e.c_str());
    }
    // auto classInfo = classMap_[className];
    auto instName = varName.empty() ? className + "_inst" : varName;

    // Stack example:
    // auto instance = builder->CreateAlloca(classType, nullptr, varName);

    // malloc example:
    auto instance = mallocInsance(classType, "GC_malloc");
    // initialize the vtable
    auto vtablePtr = builder->CreateStructGEP(classType, instance, 0, "vtable");
    auto vtableGlobal = module->getGlobalVariable(className + "_vtable_var");
    if (vtableGlobal == nullptr) {
        auto e = "Vtable not found for class: " + className;
        throw std::runtime_error(e.c_str());
    }
    builder->CreateStore(vtableGlobal, vtablePtr);

    // call the constructor
    auto constructor = module->getFunction(className + "_constructor");
    if (constructor == nullptr) {
        auto e = "Constructor not found for class: " + className;
        throw std::runtime_error(e.c_str());
    }
    auto args = genMethodArgs(instance, exp, 2, env);
    dprintf("Creating class instance: %s...\n", instName.c_str());
    env->define(instName, instance, classType);
    dprintf("Creating class instance: %s\n", instName.c_str());
    builder->CreateCall(constructor, args);
    return instance;
}

/**
 * Malloc a class instance
 */
llvm::Value*
EvaLLVM::mallocInsance(llvm::StructType* classType, const std::string& name) {
    auto instance = builder->CreateCall(
        module->getFunction("GC_malloc"),
        builder->getInt32(getTypeSize(classType)),
        name);
    return builder->CreateBitCast(instance, classType->getPointerTo());
}

/**
 * Get type size
 */
size_t EvaLLVM::getTypeSize(llvm::Type* type) {
    return module->getDataLayout().getTypeAllocSize(type);
}

/**
 * Create a Class
 */
void EvaLLVM::createClass(const Exp& exp, Env env) {
    // check size of the list
    if (exp.list.size() != 4) {
        throw std::runtime_error("Invalid class definition");
    }
    auto className = exp.list[1].string;
    auto classParent = exp.list[2].string;
    auto classBody = exp.list[3];
    // printf("Creating class %s\n", className.c_str());
    // printf("Parent class %s\n", classParent.c_str());

    // current class
    classType = llvm::StructType::create(*context, className);

    inheritClass(classType, classParent);
    classMap_[className].classType = classType;
    classMap_[className].parent = classParent;

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
void EvaLLVM::buildClassInfo(
    llvm::StructType* classType, const Exp& exp, Env env) {
    auto className = exp.list[1].string;
    // check size of the list
    if (exp.list.size() != 4) {
        throw std::runtime_error("Invalid class definition");
    }
    auto classBody = exp.list[3];

    // first element must be a string "begin"
    if (classBody.list[0].string != "begin") {
        throw std::runtime_error("Invalid class body, missing 'begin' element");
    }
    dprintf(
        "Building class info, first element: %s\n",
        classBody.list[0].string.c_str());

    // Shallow scanning of the class body, we need to know all the fields,
    // methods and their types
    for (size_t i = 1; i < classBody.list.size(); i++) {
        auto& beginLE = classBody.list[i];

        // everything else must be a list
        if (beginLE.type != ExpType::LIST) {
            throw std::runtime_error(
                "Invalid class body, expected list element");
        }
        dprintf("Building class info, element: %s\n", exp2str(beginLE).c_str());
        const auto firstLE = beginLE.list[0].string;

        // if var, update a struct
        if (firstLE == "var") {
            auto& expName = beginLE.list[1];
            auto& expInit = beginLE.list[2];

            auto varNameDecl = extractVarName(expName);
            auto varType = extractVarType(expName);
            addFieldToClass(
                className, varNameDecl, varType.type, varType.ptrType);
            dprintf(
                "Building class info, var: %s.%s, type %s, ptr type %s...\n",
                className.c_str(),
                varNameDecl.c_str(),
                dumpValueToString(varType.type).c_str(),
                dumpValueToString(varType.ptrType).c_str());
            if (expName.string == "SetFunctor.cell")
                exit(-1);
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
                throw std::runtime_error("First argument must be 'self'");
            }
            auto fn = createFunctionProto(
                className + "_" + fnName.string,
                llvm::FunctionType::get(retType, argTypes, false),
                env);
            addMethodToClass(className, fnName.string, fn);
            dprintf("Building class info, method: %s\n", fnName.string.c_str());

        } else {
            // printf("Unknown class body element: %s\n", firstLE.c_str());
            throw std::runtime_error("Invalid class body element");
        }
    }
    // create vtable, it's just a pointer array
    auto vtableType =
        llvm::StructType::create(*context, className + "_vtable_type");
    const auto vtableFields = serializeMethodTypes(className);
    vtableType->setBody(vtableFields);
    // create global variable for the vtable
    auto vtableGlobal = new llvm::GlobalVariable(
        *module,
        vtableType,
        true,
        llvm::GlobalValue::ExternalLinkage,
        nullptr,
        className + "_vtable_var");
    std::vector<llvm::Constant*> vtableInit;
    for (const auto& methodName : classMap_[className].methodNames) {
        const auto fn = classMap_[className].methodTypes[methodName];
        if (fn == nullptr) {
            auto e = "Method not found: " + className + "_" + methodName;
            throw std::runtime_error(e.c_str());
        }
        vtableInit.push_back(fn);
    }

    vtableGlobal->setInitializer(
        llvm::ConstantStruct::get(vtableType, vtableInit));
    vtableGlobal->setAlignment(llvm::MaybeAlign(8));

    // init struct fields
    const auto fields = serializeFieldTypes(vtableType, className);
    classType->setBody(fields);

    dprintf("Class info built: %s\n", dumpValueToString(classType).c_str());
}

/**
 * Inherit a class
 */
void EvaLLVM::inheritClass(
    llvm::StructType* classType, const std::string& name) {
    const auto currentClassName = classType->getName().str();
    if (name != "null") {
        const auto varName = classMap_[name].parent;
        classMap_[currentClassName].fieldNames = classMap_[name].fieldNames;
        classMap_[currentClassName].fieldTypes = classMap_[name].fieldTypes;
        classMap_[currentClassName].methodNames = classMap_[name].methodNames;
        classMap_[currentClassName].methodTypes = classMap_[name].methodTypes;
    }
}

/**
 * Get a class by name
 */
llvm::StructType* EvaLLVM::getClassByName(const std::string& name) {
    return llvm::StructType::getTypeByName(*context, name);
}

/**
 * Get the return type
 */
llvm::Type* EvaLLVM::getRetType(const Exp& exp) {
    if (exp.list.size() == 4) {
        return builder->getInt32Ty();
    } else if (exp.list.size() == 6) {
        auto possibleArrowStr = exp.list[3];
        if (possibleArrowStr.type == ExpType::SYMBOL &&
            possibleArrowStr.string == "->") {
            auto retType = exp.list[4];
            if (retType.string == "number") {
                return builder->getInt32Ty();
            } else if (retType.string == "string") {
                return builder->getPtrTy();
            } else if (classMap_.find(retType.string) != classMap_.end()) {
                return classMap_[retType.string].classType->getPointerTo();
            } else {
                throw std::runtime_error("Invalid return type");
            }
        }
    }
    dprintf("Unknown return type, assuming int\n");
    return builder->getInt32Ty();
}

/**
 * Get the argument types
 * TODO: merge with extractVarType
 */
std::vector<llvm::Type*> EvaLLVM::getArgTypes(const Exp& exp) {
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
                        // try class type
                        auto classType = getClassByName(argType);
                        if (classType != nullptr) {
                            argTypes.push_back(classType->getPointerTo());
                        } else {
                            throw std::runtime_error("Invalid argument type");
                        }
                    }
                } else {
                    throw std::runtime_error("Invalid argument declaration");
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
                throw std::runtime_error("Invalid argument declaration");
            }
        }
    }
    return argTypes;
}

/**
 * Get the argument names
 */
std::vector<std::string> EvaLLVM::getArgNames(const Exp& exp) {
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
                throw std::runtime_error("Invalid argument declaration");
            }
        }
    }
    return argNames;
}

/**
 * Extract the variable name
 */
std::string EvaLLVM::extractVarName(const Exp& varDecl) {
    if (varDecl.type == ExpType::SYMBOL) {
        return varDecl.string;
    } else if (varDecl.type == ExpType::LIST) {
        return varDecl.list[0].string;
    } else {
        throw std::runtime_error("Invalid variable declaration");
    }
    return "";
}

/**
 * Extract the variable type
 */
TypeType EvaLLVM::extractVarType(const Exp& varDecl) {
    if (varDecl.type == ExpType::SYMBOL) {
        return {builder->getInt32Ty(), nullptr};
    } else if (varDecl.type == ExpType::LIST) {
        if (varDecl.list[1].string == "number") {
            return {builder->getInt32Ty(), nullptr};
        } else if (varDecl.list[1].string == "string") {
            return {builder->getPtrTy(), nullptr};
        } else {
            // try class type
            auto classType = classMap_[varDecl.list[1].string].classType;
            if (classType != nullptr) {
                return {builder->getPtrTy(), classType};
            }
            auto s = "Unknown variable type for '" + exp2str(varDecl) + "'";
            throw std::runtime_error(s.c_str());
        }
    } else {
        throw std::runtime_error("Invalid variable declaration");
    }
    dprintf(
        "Unknown variable type for '%s', assuming int\n",
        varDecl.string.c_str());
    return {builder->getInt32Ty(), nullptr};
}

/**
 * Allocate a variable
 */
llvm::AllocaInst*
EvaLLVM::allocVar(const std::string& varName, llvm::Type* varTy, Env env) {
    varsBuilder->SetInsertPoint(&fn->getEntryBlock());
    auto var = varsBuilder->CreateAlloca(varTy, nullptr, varName);
    // if varTy is a pointer we must find the original type
    dprintf(
        "Allocating var: %s, type %s\n",
        varName.c_str(),
        dumpValueToString(varTy).c_str());
    return var;
}

/**
 * Creates a global variable
 */
llvm::GlobalVariable*
EvaLLVM::createGlobalVar(const std::string& name, llvm::Constant* init) {

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
void EvaLLVM::setupExternalFunctions() {
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
void EvaLLVM::saveModuleToFile(const std::string& fileName) {
    std::error_code      errorCode;
    llvm::raw_fd_ostream outLL(fileName, errorCode);
    module->print(outLL, nullptr);
}

EvaLLVM::EvaLLVM() {
    moduleInit();
    setupExternalFunctions();
    setupGlobalEnvironment();
    setupTargetTriple();
};

EvaLLVM::~EvaLLVM() {
    // cleanup
}

/**
 * Add field to a class
 */
void EvaLLVM::addFieldToClass(
    const std::string& className,
    const std::string& fieldName,
    llvm::Type*        fieldType,
    llvm::Type*        ptrType) { // originial type if pointer
    if (classMap_[className].fieldTypes.find(fieldName) !=
        classMap_[className].fieldTypes.end()) {
        auto e = "Field already exists: " + className + "." + fieldName;
        throw std::runtime_error(e.c_str());
    }

    dprintf(
        "Adding field to class: %s.%s\n", className.c_str(), fieldName.c_str());
    classMap_[className].fieldNames.push_back(fieldName);
    classMap_[className].fieldTypes[fieldName] = {fieldType, ptrType};
}

/**
 * Add method to a class
 */
void EvaLLVM::addMethodToClass(
    const std::string& className,
    const std::string& methodName,
    llvm::Function*    method) {
    dprintf(
        "Adding method to class: %s.%s\n",
        className.c_str(),
        methodName.c_str());
    // during overloading we might have the same method name, in which case
    // we don't update methodNames but only the methodTypes
    if (classMap_[className].methodTypes.find(methodName) ==
        classMap_[className].methodTypes.end()) {
        classMap_[className].methodNames.push_back(methodName);
    }
    classMap_[className].methodTypes[methodName] = method;
}

/**
 * Serialize field types
 */
std::vector<llvm::Type*> EvaLLVM::serializeFieldTypes(
    const llvm::StructType* vtable, const std::string& className) {
    std::vector<llvm::Type*> result;
    // first field is always a pointer to the vtable
    result.push_back(vtable->getPointerTo());
    for (const auto& fnName : classMap_[className].fieldNames) {
        result.push_back(classMap_[className].fieldTypes[fnName].type);
    }
    return result;
}

/**
 * Serialize method types
 */
std::vector<llvm::Type*>
EvaLLVM::serializeMethodTypes(const std::string& className) {
    std::vector<llvm::Type*> result;
    for (const auto& fnName : classMap_[className].methodNames) {
        result.push_back(classMap_[className]
                             .methodTypes[fnName]
                             ->getType()
                             ->getPointerTo());
    }
    return result;
}

void EvaLLVM::moduleInit() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("EvaLLVM", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
    varsBuilder = std::make_unique<llvm::IRBuilder<>>(*context);
    parser = std::make_unique<syntax::EvaParser>();
}

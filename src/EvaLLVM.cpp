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
    std::map<std::string, EnvValueType> globalObject;
    globalObject["VERSION"] = Environment::make_value(builder->getInt32(10));

    std::map<std::string, EnvValueType> globalRecord{};

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
llvm::Function* EvaLLVM::createFunction(const std::string& fnName,
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
llvm::Function* EvaLLVM::createFunctionProto(const std::string& fnName,
                                             llvm::FunctionType* fnType,
                                             Env env) {
    auto fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                     fnName, *module);
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
llvm::BasicBlock* EvaLLVM::createBB(const std::string& name,
                                    llvm::Function* fn) {
    return llvm::BasicBlock::Create(*context, name, fn);
}

/**
 * Main compile loop
 */
llvm::Value* EvaLLVM::gen(const Exp& exp, Env env) {

    const auto expType = exp_type2str(exp.type);
    if (expType == "LIST") {
        const auto s = exp_list2str(exp.list);
        dprintf("gen LIST: %s\n", s.c_str());
    } else {
        dprintf("gen '%s': '%s'\n", expType.c_str(), exp.string.c_str());
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

        // printf("Calling a function: %s\n", exp.string.c_str());
        auto fn = module->getFunction(exp.string);
        if (fn != nullptr) {
            dprintf("Function found: %s\n", exp.string.c_str());
            return builder->CreateCall(fn);
        }

        // Variables:
        // printf("Looking up variable: %s\n", exp.string.c_str());
        auto varName = exp.string;
        // cast to stack allocated value: AllocaInst
        auto varValue =
            llvm::dyn_cast<llvm::AllocaInst>(env->lookup_value(varName));
        // 1. Local variables:
        if (varValue) {
            return builder->CreateLoad(varValue->getAllocatedType(), varValue,
                                       varName.c_str());
        }

        // last resort: variables
        auto varValuePtr = env->lookup_value(varName);
        if (varValuePtr) {
            return varValuePtr;
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
                dprintf("Variable declaration: %s\n", varName.c_str());
                // special case for class properties
                if (classType != nullptr) {
                    return builder->getInt32(0);
                }
                // Class instance creation
                // (var p (new Point 1 2))
                if (varInitDecl.type == ExpType::LIST &&
                    varInitDecl.list[0].string == "new") {
                    return createClassInstance(varInitDecl, env, varName);
                }

                // initializer
                auto init = gen(varInitDecl, env);

                // type
                auto varTy = extractVarType(varNameDecl, varInitDecl);
                auto ptrTy = varTy->isPointerTy();
                dprintf("Variable type: %s, is ptr %d\n",
                        dumpValueToString(varTy).c_str(), ptrTy);

                // variable
                auto varBinding = allocVar(varName, varTy, env);
                dprintf("Variable binding: %s\n",
                        dumpValueToString(varBinding).c_str());

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
                auto record = std::map<std::string, EnvValueType>{};
                auto newEnv = std::make_shared<Environment>(record, env);

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
            // (set (prop self Point x) x)
            else if (tag.string == "set") {

                // if it's a property access, call the setter
                if (exp.list[1].type == ExpType::LIST &&
                    exp.list[1].list[0].string == "prop") {
                    auto newValue = gen(exp.list[2], env);
                    return accessProperty(exp.list[1], env, newValue);
                }

                auto varNameDecl = exp.list[1];
                auto varInitDecl = exp.list[2];
                auto varName = extractVarName(varNameDecl);

                // initializer
                auto init = gen(varInitDecl, env);

                // type
                auto varTy = extractVarType(varNameDecl, varInitDecl);

                // variable
                auto varBinding = llvm::dyn_cast<llvm::AllocaInst>(
                    env->lookup_value(varName));

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
                    llvm::FunctionType::get(retType, argTypes, false), env);
                Exp fnBody = exp.list[3];
                if (exp.list.size() == 6) {
                    fnBody = exp.list[5];
                }
                auto fnEnv = std::make_shared<Environment>(
                    std::map<std::string, EnvValueType>{}, env);
                auto fnArgs = fn->arg_begin();
                for (size_t i = 0; i < argNames.size(); i++) {
                    auto argName = argNames[i];
                    dprintf("Parsing args: %s, class %p\n", argName.c_str(),
                            classType);
                    fnArgs[i].setName(argName);
                    // store the argument in the function environment
                    fnEnv->define(argName, &fnArgs[i],
                                  classType); // TODO: needed?
                    // initialize the argument
                    auto arg = allocVar(argName, argTypes[i], fnEnv);
                    builder->CreateStore(&fnArgs[i], arg);
                }
                auto ret = gen(fnBody, fnEnv);
                builder->CreateRet(ret);

                auto typeStr = dumpValueToString(fn->getFunctionType());
                dprintf("Function defined: %s %s\n", fnName.string.c_str(),
                        typeStr.c_str());
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
                return nullptr;
            }

            // ----------------------------------------------------
            // Property access getter
            // (prop Point p x)
            else if (tag.string == "prop") {
                return accessProperty(exp, env);
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
                auto type = env->lookup(instName).type;
                dprintf("Method call: %s.%s class %p\n", instName.c_str(),
                        methodName.c_str(), type);
                // original class name
                auto className = type->getStructName().str();
                dprintf("Class name: %s\n", className.c_str());
                if (!specifiedType.empty()) {
                    className = specifiedType;
                }
                dprintf("Specified class name: %s\n", className.c_str());

                auto funcName = className + "_" + methodName;
                auto classInfo = classMap_[className];
                llvm::Value* fnDest = nullptr;
                // we're only using vtable if outside of a class, we must use
                // direct function call inside of a class
                if (classType == nullptr) {
                    dprintf("Method call outside of class: %s.%s\n",
                            className.c_str(), methodName.c_str());
                    // get fn from vtable
                    auto idx = getMethodIndex(type, methodName);
                    auto vtableGlobalVar =
                        module->getGlobalVariable(className + "_vtable_var");
                    auto vtableType = vtableGlobalVar->getValueType();
                    // fetch vtable pointer from the instance
                    auto vtablePtr =
                        builder->CreateStructGEP(type, inst, 0, "vtable_gep");
                    auto vtable = builder->CreateLoad(
                        vtableType->getPointerTo(), vtablePtr, "vtable");
                    // fetch the method pointer from the vtable
                    auto fnPtr = builder->CreateStructGEP(vtableType, vtable,
                                                          idx, "method");
                    fnDest =
                        builder->CreateLoad(classInfo.methodTypes[methodName]
                                                ->getType()
                                                ->getPointerTo(),
                                            fnPtr, "method");
                } else {
                    fnDest = module->getFunction(funcName);
                }

                if (fnDest == nullptr) {
                    auto e = "Method not found: " + funcName;
                    throw std::runtime_error(e.c_str());
                }
                std::vector<llvm::Value*> args;
                args.push_back(inst);
                for (size_t i = 3; i < exp.list.size(); i++) {
                    args.push_back(gen(exp.list[i], env));
                }
                dprintf("Calling method: %s\n", funcName.c_str());

                return builder->CreateCall(
                    classInfo.methodTypes[methodName]->getFunctionType(),
                    fnDest, args);
            }

            // ----------------------------------------------------
            // Function call
            // (square 2)
            // try to find the function in the environment
            dprintf("Looking up function: %s\n", tag.string.c_str());
            // auto fn =
            //     llvm::dyn_cast<llvm::Function>(env->lookup(tag.string));
            auto fn = module->getFunction(tag.string);
            dprintf("Lookup result: %s\n",
                    fn ? "Function found" : "Function not found");
            // auto anyFn = module->getFunction(tag.string);
            if (fn) {
                std::vector<llvm::Value*> args;
                for (size_t i = 1; i < exp.list.size(); i++) {
                    args.push_back(gen(exp.list[i], env));
                }
                return builder->CreateCall(fn, args);
            }
        }
        break;
    } // case LIST
    } // switch
    printf("Not handled %s: %s\n", exp_type2str(exp.type).c_str(),
           exp.type == ExpType::SYMBOL ? exp.string.c_str()
                                       : exp_list2str(exp.list).c_str());
    assert(false && "Not implemented");
}

/**
 * Access a property
 * If newValue is provided it's a setter
 * If newValue is nullptr it's a getter
 */
llvm::Value* EvaLLVM::accessProperty(const Exp& exp, Env env,
                                     llvm::Value* newValue) {
    dprintf("calling gen to resolve prop...\n");
    auto instName = exp.list[1].string;
    auto varName = exp.list[2].string;
    auto inst = gen(instName, env);
    auto type = env->lookup(instName).type;
    auto className = type->getStructName().str();
    dprintf("resolved prop... %s\n", dumpValueToString(inst).c_str());
    auto classInfo = classMap_[className];
    dprintf("Getting property %s from %s\n", varName.c_str(),
            dumpValueToString(type).c_str());
    if (type == nullptr) {
        auto e = "Class not found: " + varName;
        throw std::runtime_error(e.c_str());
    }
    const auto structIdx = getFieldIndex(type, varName);
    if (newValue != nullptr) {
        auto propPtr =
            builder->CreateStructGEP(type, inst, structIdx, "propPtr");
        builder->CreateStore(newValue, propPtr, "prop");
        return builder->getInt32(0);
    } else {
        auto propPtr =
            builder->CreateStructGEP(type, inst, structIdx, "propPtr");
        return builder->CreateLoad(classInfo.fieldTypes[varName], propPtr,
                                   "prop");
    }
}

/**
 * Get the struct index
 */
size_t EvaLLVM::getFieldIndex(llvm::Type* type, const std::string& field) {
    auto structName = type->getStructName().str();
    dprintf("Getting index for %s.%s\n", structName.c_str(), field.c_str());
    auto classInfo = classMap_[structName];
    size_t idx = 1; // first element is the vtable
    for (const auto& f : classInfo.fieldNames) {
        if (f == field) {
            dprintf("Field found: %s.%s at index %lu\n", structName.c_str(),
                    field.c_str(), idx);
            return idx;
        }
        idx++;
    }
    auto s = "Field not found: " + structName + "." + field;
    throw std::runtime_error(s.c_str());
}

size_t EvaLLVM::getMethodIndex(llvm::Type* type, const std::string& field) {
    auto structName = type->getStructName().str();
    dprintf("Getting index for %s.%s\n", structName.c_str(), field.c_str());
    auto classInfo = classMap_[structName];
    size_t idx = 0;
    for (const auto& f : classInfo.methodNames) {
        if (f == field) {
            dprintf("Method found: %s.%s at index %lu\n", structName.c_str(),
                    field.c_str(), idx);
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
llvm::Value* EvaLLVM::createClassInstance(const Exp& exp, Env env,
                                          std::string& varName) {
    auto className = exp.list[1].string;
    auto classType = getClassByName(className);
    if (classType == nullptr) {
        auto e = "Class not found: " + className;
        throw std::runtime_error(e.c_str());
    }
    auto classInfo = classMap_[className];
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
    std::vector<llvm::Value*> args;
    args.push_back(instance);
    for (size_t i = 2; i < exp.list.size(); i++) {
        args.push_back(gen(exp.list[i], env));
    }
    dprintf("Creating class instance: %s...\n", instName.c_str());
    env->define(instName, instance, classType);
    dprintf("Creating class instance: %s\n", instName.c_str());
    builder->CreateCall(constructor, args);
    return instance;
}

/**
 * Malloc a class instance
 */
llvm::Value* EvaLLVM::mallocInsance(llvm::StructType* classType,
                                    const std::string& name) {
    auto instance =
        builder->CreateCall(module->getFunction("GC_malloc"),
                            builder->getInt32(getTypeSize(classType)), name);
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
void EvaLLVM::buildClassInfo(llvm::StructType* classType, const Exp& exp,
                             Env env) {
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
    dprintf("Building class info, first element: %s\n",
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
        dprintf("Building class info, element: %s\n",
                exp_list2str(beginLE.list).c_str());
        const auto firstLE = beginLE.list[0].string;

        // if var, update a struct
        if (firstLE == "var") {
            auto& expName = beginLE.list[1];
            auto& expInit = beginLE.list[2];

            auto varNameDecl = extractVarName(expName);
            auto varType = extractVarType(expName, expInit);
            addFieldToClass(className, varNameDecl, varType);
            dprintf("Building class info, var: %s.%s, init...\n",
                    className.c_str(), varNameDecl.c_str());
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
                llvm::FunctionType::get(retType, argTypes, false), env);
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
        *module, vtableType, true, llvm::GlobalValue::ExternalLinkage, nullptr,
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
void EvaLLVM::inheritClass(llvm::StructType* classType,
                           const std::string& name) {
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
        auto possibleArrowStr = exp.list[4];
        if (possibleArrowStr.type == ExpType::SYMBOL &&
            possibleArrowStr.string == "->") {
            auto retType = exp.list[5];
            if (retType.string == "number") {
                return builder->getInt32Ty();
            } else if (retType.string == "string") {
                return builder->getPtrTy();
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
                        throw std::runtime_error("Invalid argument type");
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
llvm::Type* EvaLLVM::extractVarType(const Exp& varDecl, const Exp& varInit) {
    if (varDecl.type == ExpType::SYMBOL) {
        if (varInit.type == ExpType::NUMBER) {
            return builder->getInt32Ty();
        } else if (varInit.type == ExpType::STRING) {
            return builder->getPtrTy();
        } else if (varInit.type == ExpType::LIST) {
            // TODO: proper type for functions
            return builder->getInt32Ty();
            // auto s = "Unknown variable type for '" + varDecl.string +
            // "'"; throw std::runtime_error(s.c_str());
        }
    } else if (varDecl.type == ExpType::LIST) {
        if (varDecl.list[1].string == "number") {
            return builder->getInt32Ty();
        } else if (varDecl.list[1].string == "string") {
            return builder->getPtrTy();
        } else {
            auto s = "Unknown variable type for '" + varDecl.string + "'";
            throw std::runtime_error(s.c_str());
        }
    } else {
        throw std::runtime_error("Invalid variable declaration");
    }
    dprintf("Unknown variable type for '%s', assuming int\n",
            varDecl.string.c_str());
    return builder->getInt32Ty();
}

/**
 * Allocate a variable
 */
llvm::AllocaInst* EvaLLVM::allocVar(const std::string& varName,
                                    llvm::Type* varTy, Env env) {
    varsBuilder->SetInsertPoint(&fn->getEntryBlock());
    auto var = varsBuilder->CreateAlloca(varTy, nullptr, varName);
    env->define(varName, var, classType ? classType : varTy);
    return var;
}

/**
 * Creates a global variable
 */
llvm::GlobalVariable* EvaLLVM::createGlobalVar(const std::string& name,
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
    std::error_code errorCode;
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
void EvaLLVM::addFieldToClass(const std::string& className,
                              const std::string& fieldName,
                              llvm::Type* fieldType) {
    if (classMap_[className].fieldTypes.find(fieldName) !=
        classMap_[className].fieldTypes.end()) {
        auto e = "Field already exists: " + className + "." + fieldName;
        throw std::runtime_error(e.c_str());
    }

    dprintf("Adding field to class: %s.%s\n", className.c_str(),
            fieldName.c_str());
    classMap_[className].fieldNames.push_back(fieldName);
    classMap_[className].fieldTypes[fieldName] = fieldType;
}

/**
 * Add method to a class
 */
void EvaLLVM::addMethodToClass(const std::string& className,
                               const std::string& methodName,
                               llvm::Function* method) {
    dprintf("Adding method to class: %s.%s\n", className.c_str(),
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
std::vector<llvm::Type*>
EvaLLVM::serializeFieldTypes(const llvm::StructType* vtable,
                             const std::string& className) {
    std::vector<llvm::Type*> result;
    // first field is always a pointer to the vtable
    result.push_back(vtable->getPointerTo());
    for (const auto& fnName : classMap_[className].fieldNames) {
        result.push_back(classMap_[className].fieldTypes[fnName]);
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

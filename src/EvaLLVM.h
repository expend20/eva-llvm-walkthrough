#ifndef EvaLLVM_h
#define EvaLLVM_h

#include "TypesMisc.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>

// Forward declarations for EvaParser.h
enum class ExpType;
struct Exp;
namespace syntax {
class EvaParser;
} // namespace syntax

// Forward declarations for Environment.h
class Environment;

using Env = std::shared_ptr<Environment>;

/**
 * Class information
 */
struct ClassInfo {
    llvm::StructType* classType;
    std::string       parent;
    // for serialization purposes we need to keep the order of fields
    std::vector<std::string>               fieldNames;
    std::map<std::string, TypeType>        fieldTypes;
    std::vector<std::string>               methodNames;
    std::map<std::string, llvm::Function*> methodTypes;
};

std::string exp_type2str(ExpType type);
std::string exp2str(const Exp& exp);

void dprintf(const char* fmt, ...);

template <typename T> std::string dumpValueToString(const T* V);

class EvaLLVM {

  public:
    EvaLLVM();
    ~EvaLLVM();

    void setupTargetTriple();

    void eval(
        const std::string& program,
        const std::string& fileName = "./output.ll");

  private:
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

  private:
    void moduleInit();

    void setupGlobalEnvironment();

    void compile(const Exp& ast);

    llvm::Function* createFunction(
        const std::string& fnName, llvm::FunctionType* fnType, Env env);

    llvm::Function* createFunctionProto(
        const std::string& fnName, llvm::FunctionType* fnType, Env env);

    void createFunctionBlock(llvm::Function* fn);

    llvm::BasicBlock*
    createBB(const std::string& name, llvm::Function* fn = nullptr);

    ValueType gen(const Exp& exp, Env env);

    ValueType
    accessProperty(const Exp& exp, Env env, llvm::Value* newValue = nullptr);

    size_t getFieldIndex(llvm::Type* type, const std::string& field);

    size_t
    getMethodIndex(const std::string& className, const std::string& method);

    llvm::Value*
    createClassInstance(const Exp& exp, Env env, std::string& varName);

    llvm::Value*
    mallocInsance(llvm::StructType* classType, const std::string& name);

    size_t getTypeSize(llvm::Type* type);

    void createClass(const Exp& exp, Env env);

    void buildClassInfo(llvm::StructType* classType, const Exp& exp, Env env);

    void inheritClass(llvm::StructType* classType, const std::string& name);

    llvm::StructType* getClassByName(const std::string& name);

    llvm::Type* getRetType(const Exp& exp);

    std::vector<llvm::Type*> getArgTypes(const Exp& exp);

    std::vector<std::string> getArgNames(const Exp& exp);

    std::string extractVarName(const Exp& varDecl);

    TypeType extractVarType(const Exp& varDecl);

    llvm::AllocaInst*
    allocVar(const std::string& varName, llvm::Type* varTy, Env env);

    llvm::GlobalVariable*
    createGlobalVar(const std::string& name, llvm::Constant* init);

    void setupExternalFunctions();

    void saveModuleToFile(const std::string& fileName);

    void addFieldToClass(
        const std::string& className,
        const std::string& fieldName,
        llvm::Type*        fieldType,
        llvm::Type*        ptrType = nullptr);

    void addMethodToClass(
        const std::string& className,
        const std::string& funcName,
        llvm::Function*    funcType);

    std::vector<llvm::Type*> serializeFieldTypes(
        const llvm::StructType* vtable, const std::string& className);
    std::vector<llvm::Type*> serializeMethodTypes(const std::string& className);

    llvm::Value* loadVtablePtr(
        llvm::Value*       inst,
        const std::string& methodName,
        const std::string& className);

    llvm::Value* getCallable(const Exp& exp, Env env);

    std::vector<llvm::Value*>
    genFunctionArgs(const Exp& exp, size_t start, Env env);

    std::vector<llvm::Value*>
    genMethodArgs(llvm::Value* inst, const Exp& exp, size_t start, Env env);

    ClassInfo* getClassInfoByVarName(const std::string& varName, Env env);
};

#endif // EvaLLVM_h

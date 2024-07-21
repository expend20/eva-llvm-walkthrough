#ifndef EvaLLVM_h
#define EvaLLVM_h

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
    llvm::StructType* parent;
    std::map<std::string, llvm::Type*> fields;
    std::map<std::string, llvm::Function*> methods;
};

std::string exp_type2str(ExpType type);

std::string exp_list2str(const std::vector<Exp>& list);

template <typename T> std::string dumpValueToString(const T* V);

class EvaLLVM {

  public:

    EvaLLVM();
    ~EvaLLVM();

    void setupTargetTriple();

    void eval(const std::string& program,
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

    llvm::Function* createFunction(const std::string& fnName,
                                   llvm::FunctionType* fnType, Env env);

    llvm::Function* createFunctionProto(const std::string& fnName,
                                        llvm::FunctionType* fnType, Env env);

    void createFunctionBlock(llvm::Function* fn);

    llvm::BasicBlock* createBB(const std::string& name,
                               llvm::Function* fn = nullptr);

    llvm::Value* gen(const Exp& exp, Env env);

    llvm::Value* accessProperty(const Exp& exp, Env env,
                                llvm::Value* newValue = nullptr);

    size_t getStructIndex(llvm::Type* type, const std::string& field);

    llvm::Value* createClassInstance(const Exp& exp, Env env,
                                     std::string& varName);

    llvm::Value* mallocInsance(llvm::StructType* classType,
                               const std::string& name);

    size_t getTypeSize(llvm::Type* type);

    void createClass(const Exp& exp, Env env);

    void buildClassInfo(llvm::StructType* classType, const Exp& exp, Env env);

    void inheritClass(llvm::StructType* classType, const std::string& name);

    llvm::StructType* getClassByName(const std::string& name);

    llvm::Type* getRetType(const Exp& exp);

    std::vector<llvm::Type*> getArgTypes(const Exp& exp);

    std::vector<std::string> getArgNames(const Exp& exp);

    std::string extractVarName(const Exp& varDecl);

    llvm::Type* extractVarType(const Exp& varDecl, const Exp& varInit);

    llvm::AllocaInst* allocVar(const std::string& varName, llvm::Type* varTy,
                               Env env);

    llvm::GlobalVariable* createGlobalVar(const std::string& name,
                                          llvm::Constant* init);

    void setupExternalFunctions();

    void saveModuleToFile(const std::string& fileName);
};

#endif // EvaLLVM_h

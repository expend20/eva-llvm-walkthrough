#ifndef Envinroment_h
#define Envinroment_h

#include "EvaLLVM.h" // for dumpValueToString, dprintf
#include "TypesMisc.h"
#include <map>
#include <string>

#include <llvm/IR/Value.h>

/**
 * Environment: name storage
 */

class Environment : public std::enable_shared_from_this<Environment> {
  public:
    /**
     * Creates an environment with a parent link
     */
    Environment(
        std::map<std::string, ValueType> record,
        std::shared_ptr<Environment>     parent)
        : record_(record), parent_(parent) {}

    static ValueType
    make_value(llvm::Value* value, llvm::Type* type = nullptr) {
        return {value, type};
    }

    /**
     * Create a variable with a given name and value.
     * Note: for the pointers we need to specify the original type (because of
     * opaque pointers)
     */
    llvm::Value* define(
        const std::string& name, llvm::Value* value, llvm::Type* typeForPtr) {
        // check if value is a pointer
        if (value->getType()->isPointerTy()) {
            if (typeForPtr == nullptr) {
                std::string msg = "Type is required for pointers: " + name;
                throw std::runtime_error(msg);
            }
        }
        record_[name] = {value, typeForPtr};
        dprintf(
            "Env var defined: name %s, value %s, type %s\n",
            name.c_str(),
            dumpValueToString(value).c_str(),
            dumpValueToString(typeForPtr).c_str());

        return value;
    }

    /**
     * Get the value of a variable with a given name
     */
    llvm::Value* lookup_value(const std::string& name) {
        return resolve(name)->record_[name].value;
    }

    ValueType lookup(const std::string& name) {
        return resolve(name)->record_[name];
    }

    /**
     * Dump
     */
    void dump() {
        printf("Environment Dump:\n");
        for (auto& pair : record_) {
            printf("  %s\n", pair.first.c_str());
        }
    }

  private:
    /**
     * Resolve the environment chain to find the variable
     */
    std::shared_ptr<Environment> resolve(const std::string& name) {
        if (record_.find(name) != record_.end()) {
            return shared_from_this();
        } else if (parent_ != nullptr) {
            return parent_->resolve(name);
        } else {
            throw std::runtime_error("Undefined variable: '" + name + "'");
        }
    }

    /**
     * Binding storage
     */
    std::map<std::string, ValueType> record_;

    /**
     * Parent link
     */
    std::shared_ptr<Environment> parent_;
};

#endif // Envinroment_h

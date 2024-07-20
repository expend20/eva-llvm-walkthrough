#ifndef Envinroment_h
#define Envinroment_h

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
    Environment(std::map<std::string, llvm::Value*> record,
                std::shared_ptr<Environment> parent)
        : record_(record), parent_(parent) {}

    /**
     * Create a variable with a given name and value
     */
    llvm::Value* define(const std::string& name, llvm::Value* value) {
        record_[name] = value;
        return value;
    }

    /**
     * Get the value of a variable with a given name
     */
    llvm::Value* lookup(const std::string& name) {
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
            throw std::runtime_error("Undefined variable: " + name);
        }
    }

    /**
     * Binding storage
     */
    std::map<std::string, llvm::Value*> record_;

    /**
     * Parent link
     */
    std::shared_ptr<Environment> parent_;
};

#endif // Envinroment_h

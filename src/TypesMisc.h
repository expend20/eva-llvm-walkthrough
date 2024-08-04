#ifndef __TYPESMISC_H__
#define __TYPESMISC_H__

#include <llvm/IR/DerivedTypes.h>

struct TypeType {
    llvm::Type* type = nullptr;
    llvm::Type* ptrType =
        nullptr; // for pointer types it holds the original type
};

struct ValueType {
    llvm::Value* value = nullptr;
    llvm::Type* type = nullptr; // for pointer types it holds the original type
};


#endif // __TYPESMISC_H__

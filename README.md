# Programming Language with LLVM

Based on [this labs](https://github.com/DmitrySoshnikov/eva-llvm-source).

## Build

```
cmake -B build -DLLVM_DIR=~/llvm/18.1.6-assertions-install/lib/cmake/llvm
cmake --build build --config Release && ./build/eva-llvm
```

Environment variables to set-up:
* `EVA_TESTS` - enables tests.
* `EVA_DEBUG` - enables debug output input processing.
* `EVA_COUT` - prints output to the console in addition to .ll file.

## TODO

[ ] Class fields serialization error.
[ ] Tests should be done through one executable which read from stdin.

## Environment setup

This section is my environment setup for Windows.

Build llvm with RelWithDebInfo & assertions enabled (useful for verifications
and debugging). Download the sources, then:

```
cmake -B build-assertions -DCMAKE_INSTALL_PREFIX=build-assertions-install -DLLVM_ENABLE_ASSERTIONS=ON -A x64 -T ClangCL -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-assertions --config RelWithDebInfo --target install

cmake -B build-assertions -DCMAKE_INSTALL_PREFIX=build-assertions-install -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS="clang;lld" -DCMAKE_BUILD_TYPE=Release
cmake --build build-assertions --config Release --target install -- -j$(nproc)
```

Get compile_commands.json. Make IDE work.

```
cmake -B build-ninja -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=1
copy build-ninja\compile_commands.json .
```

Build & run the project.

```
cmake -B build -T ClangCL -A x64 -DLLVM_DIR=...\llvm-14.0.6-assertions-RelWithDebInfo\lib\cmake\llvm
cmake --build build --config RelWithDebInfo && build\RelWithDebInfo\eva-llvm.exe && lli output.ll
echo %errorlevel%
```

Resume fast

```
vcvarsall.bat x64
set PATH=%PATH%;z:\llvm\llvm-14.0.6-assertions-RelWithDebInfo\bin

export PATH=$PATH:~/llvm/18.1.6-assertions-install/bin
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DLLVM_DIR=~/llvm/18.1.6-assertions-install/lib/cmake/llvm
cmake --build build --config Release && ./build/eva-llvm && clang-18 output.ll /usr/lib/x86_64-linux-gnu/libgc.so -o output && ./output
```

Enable assertions on release build with cmake.

```
# enabling the assertions, the way LLVM does it
add_compile_options($<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-UNDEBUG>)
if (MSVC)
  # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
  foreach (flags_var_to_scrub
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_RELWITHDEBINFO
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_C_FLAGS_RELEASE
      CMAKE_C_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS_MINSIZEREL)
    string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
      "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
  endforeach()
endif()
```

## Into lecture

* Introduction to LLVM tools
* Parsing pipeline
* LLVM Compiler
* LLVM Interpreter
* Clang
* Structure of LLVM IR
* Entry point: @main function
* LLVM Assembler
* LLVM Disassembler
* Native x86-x64 code

Local version: 16.0.5, x86_64-pc-windows-msvc
Course version: 14

```
clang++ --verion
llc --verion

clang++ -S -emit-llvm test.cpp
clang++ -o test.exe test.ll
test.exe
echo %errorlevel%
```

.ll file format description https://llvm.org/docs/LangRef.html

```
lli test.ll
llvm-as test.ll
llvm-dis test.bc
```

minimal module

```
define i32 @main() {
  ret i32 44
}
```

## Program structures | Module

* LLVM program structure
* Module container
* Context object
* IR Builder
* Emitting IR to file
* Main executable

Context
    Module
        * Target machine metadata
        * Global variables
        * Function definition
        * Function declaration
        ...
    ...

IR Builder emits IR data

Use llvm-config to set-up all the compile flags. (didn't quite work for me on
Windows)

```
llvm-config --cxxflags --ldflags --system-libs --libs core
```

NOTE: llvm-config didn't work for me on windows. I used a cmake.

## Basic numbers | Main functionBasic numbers | Main function

* Main function
* Entry point
* Basic blocks
* Terminator instructions
* Branch instructions
* Function types
* Cast instruction
* Return value
* Compiling script

Create function, insert bb and return a value

```c++
    const auto fnType = llvm::FunctionType::get(
            /* result */ llvm::Type::getInt32Ty(*context),
            /* vararg */ false));

    auto fn = llvm::Function::Create(
            fnType, llvm::Function::ExternalLinkage, "main", *module);
    llvm::verifyFunction(*fn);

    auto entry = llvm::BasicBlock::Create(*context, name, fn);
    builder->SetInsertPoint(entry);
    // now you can use module->getFunction("main")

    // just return
    builder->CreateRet(builder->getInt32(0));
```

## Strings | Printf operator

* Strings
* Global variables
* Extern functions
* Function declarations
* Function calls
* Printf operator

```c++
    // add printf declaration
    auto printfType = llvm::FunctionType::get(
        /* result */ builder->getInt32Ty(),
        /* format arg */ builder->getInt8PtrTy(),
        /* vararg */ true);
    module->getOrInsertFunction("printf", printfType);
    ...
    // call printf
    const auto printfFn = module->getFunction("printf");
    assert(printfFn && "Function 'printf' not found");
    builder->CreateCall(printfFn, helloStr);
```

## Parsing: S-expression to AST

* S-expression
* Parsing
* BNF Grammars
* The Syntax tool
* AST nodes
* Compiling expressions

BNF files describe language rules. Backus-Naur Form, the concept from 1960.

```
touch src/parser/EvaGrammar.bnf
npm install -g syntax-cli
```

https://github.com/DmitrySoshnikov/eva-llvm-source/blob/main/src/parser/EvaGrammar.bnf

```
syntax-cli -g src/EvaGrammar.bnf -m LALR1 -o src/EvaParser.h
```

> LALR1 -- Look Ahead Left-to-Right single token.


# Lecture 6: Symbols | Global variables

* Boolean values
* Variable declarations
* Variable initializers
* Global variables
* Register and Stack variables
* Variable access

Three types of variables: Global, Register, Stack Variables.

Global Variables. Stored outside of the function, mutable, but can have only constant
initializers.

```
@foo = global i32 42, align 4
```

Register Variables. Static Single Assignment (SSA) form, no memory manipulations,
temporary results of operations.

```
%1 = add i32 1, 2
%x = add i32 %1, 3
```

Stack Variables. Load / Store local variables on the stack, names are SSA.

```
@bar = alloca i32
store i32 42, i32* %bar
%1 = load i32, i32* %bar
```

# Lecture 7: Blocks | Environments

* Blocks: groups of expressions
* Blocks: local scope
* Environment
* Scope chain
* Variable lookup
* The Global Environment

Block Scope

```c++
int x = 10;
cout << x; // 10
{
    int x = 20;
    cout << x; // 20
}
cout << x; // 10
```

=>

```eva
(var x 10)
(printf "%d" x) // 10
(begin
  (var x 20)
  (printf "%d" x) // 20
)
(printf "%d" x) // 10
```

Environment is created every time new block scope is introduced. Enviroments
are nested, if there is no variable present in a current one, we search in a
parent environtment.

# Lecture 8: Local variables | Stack allocation

```
    varsBuilder->SetInsertPoint(&fn->getEntryBlock());
    auto var = varsBuilder->CreateAlloca(varTy, nullptr, varName);
    ...
    builder->CreateStore(init, varBinding);
    ...
    builder->CreateLoad(varValue->getAllocatedType(), varValue, varName.c_str());
```

# Lecture 9: Binary expressions | Comparison operators

```
    builder->CreateICmpEQ(lhs, rhs); // ==
    builder->CreateICmpNE() // !=
    builder->CreateICmpSLT() // <
    builder->CreateICmpSLE() // <=
    builder->CreateICmpSGT() // >
    builder->CreateICmpSGE() // >=
```

# Lecture 10: Control flow: If expressions | While loops

Phi node is a special instruction which sets the result based on the incoming
branch.

```
then3:                                            ; preds = %then
  store i32 10, ptr %x, align 4
  br label %ifcont5

else4:                                            ; preds = %then
  store i32 20, ptr %x, align 4
  br label %ifcont5

ifcont5:                                          ; preds = %else4, %then3
  %4 = phi i32 [ 10, %then3 ], [ 20, %else4 ]
```

To emit, use following code:

```
    builder->CreateBr(mergeBB);
    ...
    builder->CreateCondBr(cond, thenBB, elseBB);
    ...
    thenBB = builder->GetInsertBlock();
    ...
    auto phi = builder->CreatePHI(thenVal->getType(), 2);
    phi->addIncoming(thenVal, thenBB);
    phi->addIncoming(elseVal, elseBB);
```

# Lecture 11: Function declarations | Call expression

* Class declarations
* LLVM Struct type
* GEP instruction
* Aggregate types
* Field address calculation
* Property access
* LLVM class example

To create function with parameters:

```
    // Define the function return type and parameter types
    llvm::Type* returnType = llvm::Type::getInt32Ty(context);
    llvm::Type* paramType1 = llvm::Type::getInt32Ty(context);
    llvm::Type* paramType2 = llvm::Type::getInt32Ty(context);

    // Create the function type
    std::vector<llvm::Type*> paramTypes = { paramType1, paramType2 };
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);

    // Create the function and add it to the module
    llvm::Function* func = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "myFunction",
        module
    );
```

To name the variables:

```
    // Set names for the function parameters
    llvm::Function::arg_iterator args = func->arg_begin();
    llvm::Value* arg1 = args++;
    arg1->setName("param1");
    llvm::Value* arg2 = args++;
    arg2->setName("param2");
```

# Lecture 12: Introduction to Classes | Struct types

```
; type definition
%Point = type {
    i32, ; y [0]
    i32  ; y [1]
}

; global variable
@p = global %Point { i32 10, i32 20 }

; function/method
define void @Point_constructor(%Point* %self, i32 %x, i32 %y) {

    ; (set (prop self x) x)
    %x_ptr = getelementptr %Point, %Point* %self, i32 0, i32 0
    store i32 %x, i32* %x_ptr

    %y_ptr = getelementptr %Point, %Point* %self, i32 0, i32 1
    store i32 %y, i32* %y_ptr

    ret void
}

; main example
define i32 @main() {

    %p = alloca %Point ; stack allocation, TODO: heap allocation
    call void @Point_constructor(%Point* %p, i32 10, i32 20)

    ; access the property
    %x_ptr = getelementptr %Point, %Point* %p, i32 0, i32 0
    %x = load i32, i32* %x_ptr

    ret i32 %x
}
```

Note:
 * No property names
 * prefixed constructor
 * notes on GEP
   * first index is always zero (it's needed for multi-dimensional arrays)
   * GEP only calculates the offset

```
@str = global [15 x i8] c"hello, world\0a\00"

@px = getelementptr [15 x i8], [15 x i8]* @str, i32 0, i32 2
```

# Lecture 13: Compiling Classes

* Class declaration
* Class info structure
* Class methods
* Self parameter

# Lecture 14: Instances | Heap allocation

* New operator
* Stack allocation
* Heap allocation
* Extern malloc function
* Garbage Collection
* Mark-Sweep, libgc

GC_malloc is a garbage collector function.
```
apt-get install libgc-dev
dpkg -L libgc-dev
```

# Lecture 15: Property access

* Prop instruction
* Field index
* Struct GEP instruction
* Getters | Setters
* Load-Store architecture

# Lecture 16: Class Inheritance | vTable
* Class inheritance
* Virtual Table (vTable)
* Methods storage
* Dynamic dispatch
* Generic methods

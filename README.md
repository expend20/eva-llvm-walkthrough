# Programming Language with LLVM

[sources](https://github.com/DmitrySoshnikov/eva-llvm-source).

## Environment setup

This section is my environment setup for Windows.

Build llvm with RelWithDebInfo & assertions enabled (useful for verifications
and debugging). Download the sources, then:

```
cmake -B build-assertions -DCMAKE_INSTALL_PREFIX=build-assertions-install -DLLVM_ENABLE_ASSERTIONS=ON -A x64 -T ClangCL -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-assertions --config RelWithDebInfo --target install
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

export PATH=$PATH:~/llvm/llvm-project-llvmorg-18.1.4/llvm/build-assertions-install/bin
cmake --build build --config Release && ./build/eva-llvm && lli output.ll
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




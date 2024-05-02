#include <string>

#include "EvaLLVM.h"

int main() {
    /**
     * The program to be executed.
     */
    std::string program = R"(

        //(var VERSION 42)

        //(begin
        //  (var VERSION "Hello, World!")
        //  (printf "Block version: %s\n" VERSION )
        //)

        (printf "True: %d\n" VERSION)

    )";

    /**
     * Compiler instance.
     */
    EvaLLVM vm;

    /**
     * Generate LLVM IR.
     */
    vm.eval(program);
    return 0;
}

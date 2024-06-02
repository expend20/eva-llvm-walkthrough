#include <string>

#include "EvaLLVM.h"

int main() {
    /**
     * The program to be executed.
     */
    std::string program = R"(

        // untyped args
        (def square (x) (* x x))
        (var x 2)
        (set x (square x)) // 4
        (printf "X: %d\n" x)

        // typed args
        (def sum ((a number) (b number)) -> number (+ a b))
        (var y (sum 2 3)) // 5
        (printf "Y: %d\n" y)

        // function with no parameters
        (def foo () (begin
            (printf "Hello, World!\n")
        ))
        foo

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

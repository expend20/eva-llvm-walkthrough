#include <string>

#include "EvaLLVM.h"

int main() {
    /**
     * The program to be executed.
     */
    std::string program = R"(

        //(var x (+ 32 10))

        //(if (== x 42)
        //    (set x 100)
        //    (set x 200))

        (printf "10 > 11: %d\n" (> 10 11))
        (printf "10 < 11: %d\n" (< 10 11))
        (printf "10 == 11: %d\n" (== 10 11))
        (printf "10 == 10: %d\n" (== 10 10))
        (printf "10 != 11: %d\n" (!= 10 11))
        (printf "10 != 10: %d\n" (!= 10 10))
        (printf "10 >= 11: %d\n" (>= 10 11))
        (printf "10 <= 11: %d\n" (<= 10 11))
        (printf "10 >= 10: %d\n" (>= 10 10))
        (printf "10 <= 10: %d\n" (<= 10 10))
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

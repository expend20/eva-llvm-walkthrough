#include <string>

#include "EvaLLVM.h"

int main() {
    /**
     * The program to be executed.
     */
    std::string program = R"(

        (var x 0)

        (while (< x 5)
            (begin
                (printf "%d " x)
                (set x (+ x 1))
            )
        )

        (printf "\nX: %d\n" x)
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

#include <string>

#include "../EvaLLVM.h"

int main(int argc, char *argv[]) {
    /**
     * Parameters check.
     */

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    /**
     * The program to be executed.
     */
    std::string program = R"(

        (var x 0)

        (if (< x 2)
            (if (== x 1)
                (set x 10)
                (set x 20))
            (set x 200))
        (printf "X: %d\n" x)

    )";

    /**
     * Compiler instance.
     */
    EvaLLVM vm;

    /**
     * Generate LLVM IR.
     */
    vm.eval(program, argv[1]);
    return 0;
}


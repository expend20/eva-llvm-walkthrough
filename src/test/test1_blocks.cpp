#include <iostream>

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

        (var x 42)
        (begin
          (var x "Hello, World!")
          (printf "Block version: %s\n" x )
        )
        (printf "x: %d\n" x)
        (set x 43)
        (printf "x2: %d\n" x)

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


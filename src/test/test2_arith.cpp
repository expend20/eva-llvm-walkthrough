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

        (var x (+ 10 10))
        (var y 5)
        (set y (+ y x))
        (set y (- y 10))
        (set y (* y 100))
        (set y (/ y 20))
        (printf "x: %d\n" y)

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
    vm.eval(program, argv[1]);
    return 0;
}


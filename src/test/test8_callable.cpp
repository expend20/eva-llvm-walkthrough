
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

      // Functors - callable objects
      //
      (class Transformer null
        (begin

          (var factor 0)

          (def constructor (self factor)
            (begin
              (set (prop self factor) factor)
            )
          )

          (def __call__ (self v)
            (begin
              (printf "Transformed.__call__\n")
              (* v (prop self factor))
            )
          )
        )
      )

      (var transform (new Transformer 2))
      (var x (transform 10)) // call __call__ just by using the object
      (printf "x = %d\n" x)

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


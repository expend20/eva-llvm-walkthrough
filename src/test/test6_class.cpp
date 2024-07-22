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

      (class Point null
        (begin

          (var x 0)
          (var y 0)

          (def constructor (self x y)
            (begin
              (set (prop self x) x)
              (set (prop self y) y)
            )
          )

          (def calc (self)
            (+ (prop self x) (prop self y))
          )
        )
      )

      // constructor is called automatically
      (var p (new Point 10 20))

      // due to opaque pointers we need to specify at type
      (printf "p.x = %d\n" (prop p x))
      (printf "p.y = %d\n" (prop p y))

      (var c (method p calc))
      (printf "p.x + p.y = %d\n" c)

      (method p constructor 30 40)
      (printf "p.x + p.y = %d\n" (method p calc))

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


#include <string>

#include "EvaLLVM.h"

int main() {
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
              0
              //(set (prop self x) x)
              //(set (prop self y) y)
            )
          )

          (def calc (self)
            0 //(+ (prop self x) (prop self y))
          )
        )
      )

      (var p (new Point 1 2))
      //(print "p.x = %d\n" (prop p x))

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

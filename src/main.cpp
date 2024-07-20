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
              (set (prop Point self x) x)
              (set (prop Point self y) y)
            )
          )

          (def calc (self)
            (+ (prop Point self x) (prop Point self y))
          )
        )
      )

      (var p (new Point 10 20))
      (printf "p.x = %d\n" (prop Point p x))
      (printf "p.y = %d\n" (prop Point p y))
      (var c (Point_calc p))
      (printf "p.x + p.y = %d\n" c)

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

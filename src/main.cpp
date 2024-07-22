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
              (set (prop self x) x)
              (set (prop self y) y)
            )
          )

          (def calc (self)
            (begin
              (printf "Point.calc\n")
              (+ (prop self x) (prop self y))
            )
          )
        )
      )

      (class Point3D Point
        (begin

          (var z 0)

          (def constructor (self x y z)
            (begin
              (printf "Point3D.constructor\n")
              (super self constructor x y)
              (set (prop self z) z)
            )
          )

          (def calc (self)
            (begin
              (printf "Point3D.calc\n")
              (+ (super self calc) (prop self z))
            )
          )
        )
      )

      // constructor is called automatically
      (var p (new Point3D 10 20 30))

      (printf "p.x = %d\n" (prop p x))
      (printf "p.y = %d\n" (prop p y))
      (printf "p.z = %d\n" (prop p z))
      (printf "p.x + p.y + p.z = %d\n" (method p calc))

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

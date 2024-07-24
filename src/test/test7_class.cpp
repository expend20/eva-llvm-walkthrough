
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
              (method (self Point) constructor x y)
              (set (prop self z) z)
            )
          )

          (def calc (self)
            (begin
              (printf "Point3D.calc\n")
              (+ (method (self Point) calc) (prop self z))
            )
          )
        )
      )

      // constructor is called automatically
      (var p (new Point3D 10 20 30))

      (printf "p.x = %d\n" (prop p x))
      (printf "p.y = %d\n" (prop p y))
      (printf "p.z = %d\n" (prop p z))

      // still prints 60 (despite cast to Point) because of vtable
      (printf "p.x + p.y + p.z = %d\n" (method (p Point) calc))

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


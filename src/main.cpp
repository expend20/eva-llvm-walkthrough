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

      (class Point3D Point
        (begin

          (var z 0)

          (def constructor (self x y z)
            (begin
              (Point_constructor self x y)
              (set (prop Point3D self z) z)
            )
          )

          (def calc (self)
            (+ (Point_calc self) (prop Point3D self z))
          )
        )
      )

      // constructor is called automatically
      (var p (new Point3D 10 20 30))

      // due to opaque pointers we need to specify at type
      (printf "p.x = %d\n" (prop Point3D p x))
      (printf "p.y = %d\n" (prop Point3D p y))
      (printf "p.z = %d\n" (prop Point3D p z))

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

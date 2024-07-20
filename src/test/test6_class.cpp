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

      // constructor is called automatically
      (var p (new Point 10 20))

      // due to opaque pointers we need to specify at type
      (printf "p.x = %d\n" (prop Point p x))
      (printf "p.y = %d\n" (prop Point p y))

      // method call nonatation <Type>_<method> <object> <args>
      (var c (Point_calc p))
      (printf "p.x + p.y = %d\n" c)

      (Point_constructor p 30 40)
      (printf "p.x + p.y = %d\n" (Point_calc p))

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


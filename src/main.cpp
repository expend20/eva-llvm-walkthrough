#include <string>

#include "EvaLLVM.h"

int main() {
    /**
     * The program to be executed.
     */
    std::string program = R"(

      // Functors - callable objects (aka Closure)

      // Closures (aba Boxes) are syntactic sugar for functors
      (class Cell null
        (begin
          (var value 0)
          (def constructor (self value) -> Cell
            (begin
              (set (prop self value) value)
              self
            )
          )
        )
      )

      (class SetFunctor null
        (begin
          (var (cell Cell) 0)
          (def constructor (self (cell Cell)) -> SetFunctor
            (begin
              (set (prop self cell) cell)
              self
            )
          )
          (def __call__ (self value)
            (begin
              (var x (prop self cell))
              (set (prop x value) value)
              value
            )
          )
        )
      )

      (class GetFunctor null
        (begin
          (var (cell Cell) 0)
          (def constructor (self (cell Cell)) -> GetFunctor
            (begin
              (set (prop self cell) cell)
              self
            )
          )
          (def __call__ (self)
            (prop (prop self cell) value)
          )
        )
      )

      (var n (new Cell 10))
      (var setN (new SetFunctor n))
      (var getN (new GetFunctor n))

      (printf "n = %d\n" (getN)) // n = 10
      (printf "setN(20) = %d\n" (setN 20)) // setN(20) = 20
      (printf "n = %d\n" (getN)) // n = 20

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

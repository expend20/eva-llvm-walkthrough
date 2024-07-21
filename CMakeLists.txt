cmake_minimum_required(VERSION 3.5)
project(eva-llvm)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/add_test_executable.cmake)
include(cmake/assertions.cmake)
include(cmake/setup_llvm_package.cmake)

enable_assertions()
setup_llvm_package()

# build a library from src/EvalLLVM.cppj
add_library(eva-llvm-lib
  src/EvaLLVM.cpp
)

add_executable(eva-llvm
  src/main.cpp
)
target_link_libraries(eva-llvm
  PRIVATE LLVMAnalysis LLVMMC LLVMObject LLVMSupport eva-llvm-lib
)

# runt tests only if EVA_TESTS env var is set
if (DEFINED ENV{EVA_TESTS})
  add_test_executable(test1_blocks src/test/test1_blocks.cpp)
  add_test_executable(test2_arith src/test/test2_arith.cpp)
  add_test_executable(test3_if src/test/test3_if.cpp)
  add_test_executable(test4_while src/test/test4_while.cpp)
  add_test_executable(test5_func src/test/test5_func.cpp)
  add_test_executable_gc(test6_class src/test/test6_class.cpp)
endif()


cmake_minimum_required(VERSION 3.20.0)
project(eva-llvm)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(LLVM_TARGETS_TO_BUILD X86)

find_package(LLVM REQUIRED CONFIG
  PATHS /path/to/llvm/installation
  COMPONENTS Analysis Core Support
)
include_directories(${LLVM_INCLUDE_DIRS})

# enabling the assertions, the way LLVM does it
add_compile_options($<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-UNDEBUG>)
if (MSVC)
  # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
  foreach (flags_var_to_scrub
      CMAKE_CXX_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_RELWITHDEBINFO
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_C_FLAGS_RELEASE
      CMAKE_C_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS_MINSIZEREL)
    string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
      "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
  endforeach()
endif()

add_executable(eva-llvm
  src/main.cpp
)
target_link_libraries(eva-llvm
  PRIVATE LLVMAnalysis LLVMMC LLVMObject LLVMSupport
)

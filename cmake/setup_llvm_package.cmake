function(setup_llvm_package)
    find_package(LLVM REQUIRED CONFIG
        COMPONENTS Analysis Core Support
    )
    include_directories(${LLVM_INCLUDE_DIRS})
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
endfunction()

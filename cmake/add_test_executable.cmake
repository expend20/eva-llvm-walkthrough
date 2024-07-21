# TODO: add dependencies on test file results change (arguments for diff command)
function(add_test_executable TARGET_NAME SOURCE_FILE)
    add_executable(
        ${TARGET_NAME}
        ${SOURCE_FILE})
    target_link_libraries(
        ${TARGET_NAME}
        PRIVATE LLVMAnalysis LLVMMC LLVMObject LLVMSupport eva-llvm-lib)
    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND ${TARGET_NAME} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
        COMMAND lli ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt
        COMMAND diff ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt ${CMAKE_CURRENT_SOURCE_DIR}/src/test/expected/${TARGET_NAME}.txt
    )
endfunction()

function(add_test_executable_gc TARGET_NAME SOURCE_FILE)
    add_executable(
        ${TARGET_NAME}
        ${SOURCE_FILE})
    target_link_libraries(
        ${TARGET_NAME}
        PRIVATE LLVMAnalysis LLVMMC LLVMObject LLVMSupport eva-llvm-lib)
    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND ${TARGET_NAME} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
        # TODO: get rid of hardcoded path
        COMMAND clang-18
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
            /usr/lib/x86_64-linux-gnu/libgc.so
            -o ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME} > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt
        COMMAND diff ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt ${CMAKE_CURRENT_SOURCE_DIR}/src/test/expected/${TARGET_NAME}.txt
    )
endfunction()

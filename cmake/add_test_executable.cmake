function(add_test_executable TARGET_NAME SOURCE_FILE)
    add_executable(
        ${TARGET_NAME}
        ${SOURCE_FILE})
    target_link_libraries(
        ${TARGET_NAME} 
        PRIVATE LLVMAnalysis LLVMMC LLVMObject LLVMSupport)
    add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND ${TARGET_NAME} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
        COMMAND lli ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt
        COMMAND diff ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt ${CMAKE_CURRENT_SOURCE_DIR}/src/test/expected/${TARGET_NAME}.txt
    )
endfunction()


function(add_test_executable_gc TARGET_NAME SOURCE_FILE)
    add_custom_target(${TARGET_NAME} ALL
        # print TARGET_NAME and SOURCE_FILE
        COMMAND echo TARGET_NAME=${TARGET_NAME} SOURCE_FILE=${SOURCE_FILE}
        # print debug info

        COMMAND echo Current source directory: ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND echo "${CMAKE_CURRENT_BINARY_DIR}/eva-llvm ${SOURCE_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll"

        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/eva-llvm ${SOURCE_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
        # TODO: get rid of hardcoded path
        COMMAND clang
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.ll
            /usr/lib/x86_64-linux-gnu/libgc.so
            -o ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME} > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt
        COMMAND diff ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.txt ${CMAKE_CURRENT_SOURCE_DIR}/src/test/expected/${TARGET_NAME}.txt
        # set working directory as project root
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${SOURCE_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/test/expected/${TARGET_NAME}.txt
        COMMENT "Building and testing ${TARGET_NAME}"
    )
endfunction()

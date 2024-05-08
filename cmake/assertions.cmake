# enabling the assertions, the way LLVM does it
function(enable_assertions)
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
endfunction()




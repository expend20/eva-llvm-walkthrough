## Environment setup

This section is my environment setup, basically it's just a scratchpad for a
quick reference. This may include local paths, commands, and other stuff.

Build llvm with RelWithDebInfo & assertions enabled (useful for verifications
and debugging). Download the sources, then:

```
cmake -B build-assertions -DCMAKE_INSTALL_PREFIX=build-assertions-install -DLLVM_ENABLE_ASSERTIONS=ON -A x64 -T ClangCL -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-assertions --config RelWithDebInfo --target install

cmake -B build-assertions -DCMAKE_INSTALL_PREFIX=build-assertions-install -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_PROJECTS="clang;lld" -DCMAKE_BUILD_TYPE=Release
cmake --build build-assertions --config Release --target install -- -j$(nproc)
```

Get compile_commands.json. Make IDE work.

```
cmake -B build-ninja -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=1
copy build-ninja\compile_commands.json .
```

Build & run the project.

```
cmake -B build -T ClangCL -A x64 -DLLVM_DIR=...\llvm-14.0.6-assertions-RelWithDebInfo\lib\cmake\llvm
cmake --build build --config RelWithDebInfo && build\RelWithDebInfo\eva-llvm.exe && lli output.ll
echo %errorlevel%
```

Resume fast

```
vcvarsall.bat x64
set PATH=%PATH%;z:\llvm\llvm-14.0.6-assertions-RelWithDebInfo\bin

export PATH=$PATH:~/llvm/18.1.6-assertions-install/bin
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DLLVM_DIR=~/llvm/18.1.6-assertions-install/lib/cmake/llvm
cmake --build build --config Release && ./build/eva-llvm && clang-18 output.ll /usr/lib/x86_64-linux-gnu/libgc.so -o output && ./output
```

Enable assertions on release build with cmake.

```
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
```



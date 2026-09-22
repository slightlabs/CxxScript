#!/bin/bash -eu
#
# OSS-Fuzz build script for CxxScript.
# Compiles the library plus each libFuzzer harness, linking against
# $LIB_FUZZING_ENGINE provided by the OSS-Fuzz toolchain.

cd "$SRC/cxxscript"

# Build the interpreter library objects once.
$CXX $CXXFLAGS -std=c++17 -Iinclude -c src/*.cpp
ar rcs libcxxscript.a ./*.o

for target in fuzz_lexer fuzz_parser fuzz_script; do
  $CXX $CXXFLAGS -std=c++17 -Iinclude \
    "fuzz/${target}.cpp" libcxxscript.a \
    $LIB_FUZZING_ENGINE -o "$OUT/${target}"
done

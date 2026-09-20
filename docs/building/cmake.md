# Building with CMake

## Build from source

```bash
cmake -S . -B build
cmake --build build -j
```

### Useful CMake options

| Option | Default | Description |
|---|---|---|
| `CXXSCRIPT_BUILD_TESTS` | `ON` | Build the GoogleTest suite (fetched via `FetchContent`) |
| `CXXSCRIPT_BUILD_EXAMPLES` | `ON` | Build the example/demo executables and `cxxscript` CLI |
| `CXXSCRIPT_BUILD_FUZZERS` | `OFF` | Build libFuzzer harnesses for lexer/parser/script (requires Clang) |
| `CXXSCRIPT_BUILD_BENCHMARKS` | `OFF` | Build the `cxxscript_bench` performance runner |

Disable extras when consuming CxxScript as a dependency to avoid pulling in GoogleTest:

```bash
cmake -S . -B build -DCXXSCRIPT_BUILD_TESTS=OFF -DCXXSCRIPT_BUILD_EXAMPLES=OFF
```

## Fuzzing and benchmarks

```bash
# Fuzzing (needs clang/clang++)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DCXXSCRIPT_BUILD_FUZZERS=ON
cmake --build build-fuzz -j
./build-fuzz/fuzz/fuzz_parser corpus/ -max_total_time=60

# Benchmarks
cmake -S . -B build -DCXXSCRIPT_BUILD_BENCHMARKS=ON
cmake --build build -j && ./build/bin/cxxscript_bench
```

## Run tests

```bash
cd build
ctest --output-on-failure
# or
cmake --build . --target run_tests
```

## Install

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build --prefix /usr/local
```

Installed layout (via `GNUInstallDirs`):

- Headers: `<prefix>/include/CxxScript`
- Library: `<prefix>/lib/libCxxScript.a`
- Scripts: `<prefix>/share/CxxScript/scripts`
- CMake package config: `<prefix>/lib/cmake/CxxScript/CxxScriptConfig.cmake`

## Consuming from another CMake project

After installing (or via `add_subdirectory`/`FetchContent`):

```cmake
find_package(CxxScript CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE CxxScript::CxxScript)
```

Or embed directly as a subdirectory:

```cmake
add_subdirectory(third_party/CxxScript)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE CxxScript::CxxScript)
```

When embedding via `add_subdirectory`, set `CXXSCRIPT_BUILD_TESTS`/`CXXSCRIPT_BUILD_EXAMPLES` to
`OFF` beforehand with `set(... CACHE BOOL "" FORCE)` if you don't need them.

See [Conan](conan.md) for package-manager based consumption.

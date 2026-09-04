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
| `CXXSCRIPT_BUILD_EXAMPLES` | `ON` | Build the example/demo executables |

Disable both when consuming CxxScript as a dependency to avoid pulling in GoogleTest:

```bash
cmake -S . -B build -DCXXSCRIPT_BUILD_TESTS=OFF -DCXXSCRIPT_BUILD_EXAMPLES=OFF
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

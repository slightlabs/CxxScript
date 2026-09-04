# Building with Conan

CxxScript ships a [Conan 2.x](https://docs.conan.io/2/) recipe (`conanfile.py`) at the repository
root so it can be consumed like any other Conan package.

## Create the package locally

```bash
pip install "conan>=2.0"
conan profile detect --force   # first time only

conan create . --build=missing
```

This builds the library (tests and examples are skipped for the package build), runs the
`test_package` smoke test, and caches the package as `cxxscript/1.0.0`.

## Consume it from another Conan project

`conanfile.txt`:

```ini
[requires]
cxxscript/1.0.0

[generators]
CMakeDeps
CMakeToolchain
```

`CMakeLists.txt`:

```cmake
find_package(CxxScript CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE CxxScript::CxxScript)
```

```bash
conan install . --output-folder=build --build=missing
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
```

## Options

| Option | Default | Description |
|---|---|---|
| `shared` | `False` | Build a shared library instead of static |
| `fPIC` | `True` | Position-independent code (ignored on Windows) |

```bash
conan create . -o shared=True
```

## `test_package`

The `test_package/` directory contains a minimal consumer used by `conan create` to verify the
package installs and links correctly — it loads an inline script and checks the result of a simple
`add` procedure.

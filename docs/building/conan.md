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
`test_package` smoke test, and caches the package as `cxxscript/0.1.5`.

## Consume it from another Conan project

`conanfile.txt`:

```ini
[requires]
cxxscript/0.1.5

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

## ConanCenter submission recipe

The [recipes/cxxscript/](https://github.com/slightlabs/CxxScript/tree/main/recipes/cxxscript)
folder holds a separate recipe formatted for submission to
[conan-center-index](https://github.com/conan-io/conan-center-index), the source repo behind the
official `conancenter` remote. It differs from the root `conanfile.py` in one important way:
instead of `exports_sources`, it fetches a specific tagged release tarball declared in
`recipes/cxxscript/all/conandata.yml` (URL + sha256), which is how ConanCenter requires recipes to
obtain sources. Validate it locally the same way ConanCenter's CI would:

```bash
conan create recipes/cxxscript/all --version 0.1.5 --build=missing
```

Publishing to ConanCenter itself means forking `conan-io/conan-center-index`, copying this
`recipes/cxxscript/` folder in, and opening a pull request there — it's reviewed and built by
their CI independently of this repository.


# Getting Started

## Prerequisites

- A C++17 compiler (GCC, Clang, or MSVC)
- CMake 3.10+
- Git (used by CMake's `FetchContent` to pull GoogleTest for the test suite)

## Clone and build

```bash
git clone https://github.com/slightlabs/CxxScript.git
cd CxxScript

cmake -S . -B build
cmake --build build -j
```

This produces:

- `build/lib/libCxxScript.a` — the static library
- `build/bin/example_usage` and other demo executables
- `build/tests/*` — the GoogleTest-based test executables

## Run the tests

```bash
cd build
ctest --output-on-failure
```

## Run the example

```bash
cmake --build build --target run_example
```

## A minimal script

Create `hello.script`:

```cpp
int32 add(int32 a, int32 b) {
    return a + b;
}
```

And load/execute it from C++:

```cpp
#include "ScriptManager.h"
using namespace Script;

int main() {
    ScriptManager manager;
    std::vector<CompilationError> errors;

    if (!manager.loadScriptFile("hello.script", errors)) {
        for (auto &e : errors) std::cerr << e.toString() << "\n";
        return 1;
    }

    Value result;
    std::string errorMessage;
    manager.executeProcedure("add", {static_cast<int32_t>(2), static_cast<int32_t>(3)},
                              result, errorMessage);
    // std::get<int32_t>(result) == 5
}
```

Continue to the [Examples](examples/01-hello-world.md) section for a guided tour, from the
simplest procedure to a complete multi-file application.

## Building only the library (no tests/examples)

If you're consuming CxxScript as a dependency (for example via `add_subdirectory` or Conan), you
can skip GoogleTest and the demo binaries entirely:

```bash
cmake -S . -B build \
  -DCXXSCRIPT_BUILD_TESTS=OFF \
  -DCXXSCRIPT_BUILD_EXAMPLES=OFF
cmake --build build
```

See [Building & Packaging](building/cmake.md) for CMake install/consume instructions, and
[Conan](building/conan.md) for package manager usage.

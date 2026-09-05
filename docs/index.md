# CxxScript

**CxxScript** is a lightweight, embeddable scripting engine written in modern C++17. It lets a host
application load small, statically-typed `.script` files containing procedures, compile them with
clear error diagnostics, and execute them on demand — while still calling back into native C++ code.

```cpp
#include "ScriptManager.h"

using namespace Script;

ScriptManager manager;
std::vector<CompilationError> errors;
manager.loadScriptFile("scripts/example.script", errors);

Value result;
std::string error;
manager.executeProcedure("factorial", {static_cast<int32_t>(5)}, result, error);
// result == 120
```

## Why CxxScript?

- **Small and dependency-free** — the runtime only depends on the C++ standard library.
- **Statically typed** — every procedure has explicit parameter and return types, checked at compile time.
- **Host-friendly** — expose native functions and variables to scripts, and call script procedures from C++.
- **Multi-file** — organize logic across script modules that call each other.
- **Clear diagnostics** — compilation errors report file, procedure, line, and column.

## Feature Overview

| Category | Support |
|---|---|
| Scalar types | `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `float`, `double`, `char`, `string`, `bool` |
| Arrays | Typed arrays of any scalar, e.g. `int32[]`, with literals, indexing, `len()`, `push()`, `pop()` |
| Operators | Arithmetic (`+ - * / %`), bitwise (`& \| ^ ~ << >>`), logical (`! && \|\|` with short-circuit) |
| Control flow | `if/else`, `while`, `for`, `do-while`, `switch/case/default`, ternary `?:`, `break`/`continue` |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=` |
| Procedures | Cross-procedure and cross-file calls |
| Host integration | External function callbacks, external (read/write or read-only) variables |
| Errors | Compile-time and runtime errors with line/column/procedure context |

## Where to go next

- New to CxxScript? Start with [Getting Started](getting-started.md).
- Want to see the language in action? Browse the [Examples](examples/01-hello-world.md), which
  progress from a minimal "hello world" procedure to a full multi-file, real-world application.
- Embedding the engine in your own app? See [Embedding in C++](embedding.md).
- Consuming the library via CMake or Conan? See [Building & Packaging](building/cmake.md).

CxxScript is [MIT licensed](https://github.com/slightlabs/CxxScript/blob/main/LICENSE).

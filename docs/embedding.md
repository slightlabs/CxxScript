# Embedding in C++

`ScriptManager` is the single entry point for hosting CxxScript in your application.

## 1. Create a manager

```cpp
#include "ScriptManager.h"
using namespace Script;

ScriptManager manager;
```

## 2. (Optional) Register external functions and variables

```cpp
manager.registerExternalFunction("myFunction", [](const std::vector<Value> &args) -> Value {
    return static_cast<int32_t>(42 + std::get<int32_t>(args[0]));
});

int32_t hostValue = 10;
manager.registerExternalVariable(
    "sharedValue",
    [&]() -> Value { return static_cast<int32_t>(hostValue); },
    [&](const Value &v) { hostValue = std::get<int32_t>(v); }
);
```

See [External Functions](examples/04-external-functions.md) and
[External Variables](examples/05-external-variables.md) for more detail.

## 3. Load scripts

```cpp
std::vector<CompilationError> errors;
if (!manager.loadScriptFile("example.script", errors)) {
    for (const auto &error : errors) {
        std::cout << error.toString() << std::endl;
    }
    return;
}
```

`loadScriptSource(source, filename, errors)` loads from an in-memory string instead of a file —
useful for scripts embedded as resources or fetched at runtime.

## 4. Inspect loaded procedures (optional)

```cpp
for (const auto &name : manager.getProcedureNames()) {
    ScriptManager::ProcedureInfo info;
    manager.getProcedureInfo(name, info);
    std::cout << ValueHelper::typeToString(info.returnType) << " " << info.name << std::endl;
}
```

## 5. Execute a procedure

```cpp
std::vector<Value> arguments = {static_cast<int32_t>(10), static_cast<int32_t>(20)};
Value returnValue;
std::string errorMessage;

if (manager.executeProcedure("calculate", arguments, returnValue, errorMessage)) {
    std::cout << "Result: " << std::get<bool>(returnValue) << std::endl;
} else {
    std::cout << "Error: " << errorMessage << std::endl;
}
```

## API summary

| Method | Purpose |
|---|---|
| `loadScriptFile(filename, errors)` | Load and compile a script file |
| `loadScriptSource(source, filename, errors)` | Load and compile script from a string |
| `checkScript(filename, errors)` / `checkScriptSource(...)` | Validate without loading |
| `executeProcedure(name, args, result, error)` | Execute a loaded procedure |
| `hasProcedure(name)` | Check if a procedure is loaded |
| `getProcedureNames()` / `getProcedureInfo(name, info)` | Introspect loaded procedures |
| `registerExternalFunction(s)` / `unregisterExternalFunction` / `hasExternalFunction` | Manage host callbacks |
| `registerExternalFunctionUnary<Ret, Arg>` / `registerExternalFunctionBinary<Ret, A1, A2>` | Typed helpers for common signatures |
| `registerExternalVariable` / `registerExternalVariableReadOnly` / `unregisterExternalVariable` / `hasExternalVariable` | Expose host state |
| `setExecutionLimits(maxCallDepth, maxSteps)` / `clearExecutionLimits()` | Optional runtime guardrails (`0` = unlimited) |
| `setMemoryLimits(maxArraySize, maxStringLength, maxAllocations)` | Cap container sizes and allocations |
| `setOutputCallback(cb)` | Redirect `print`/`println` output |
| `setDebugHook(cb)` | Per-statement hook with locals snapshot and call stack |
| `setImportsEnabled(bool)` / `addImportRoot(dir)` | Restrict or disable `import` |
| `disableBuiltin(name)` / `enableBuiltin(name)` | Per-builtin kill switch |
| `setAstCacheEnabled(bool)` | Cache parsed ASTs across recompiles |
| `reloadScriptFile(filename, errors)` | Hot-reload a file's procedures |
| `evaluateSnippet(source, name, result, error)` | Evaluate top-level statements (REPL) |
| `clear()` | Reset interpreter state (removes loaded procedures and external bindings) |

See the full [API Reference](api-reference.md) for type signatures.

## Hardening: bound untrusted scripts

Execution and memory limits are **disabled by default** (`0` = unlimited). If your host executes
scripts from a semi-trusted source (config files, business rules authored by non-developers,
etc.), set explicit bounds so a malformed or malicious script fails with a runtime error instead
of overflowing the native call stack, allocating unbounded memory, or hanging the process:

```cpp
ScriptManager manager;
manager.setExecutionLimits(/*maxCallDepth=*/200, /*maxSteps=*/2'000'000);
manager.setMemoryLimits(/*maxArraySize=*/100'000,
                        /*maxStringLength=*/1'000'000,
                        /*maxAllocations=*/50'000);
```

Limits apply per top-level `executeProcedure` call and are shared across the whole call tree, so
recursion combined with looping still counts against the same budget. Violations raise a
**fatal** `RuntimeError` — script `try`/`catch` blocks cannot suppress them, though `finally`
still runs. Call `clearExecutionLimits()`/`clearMemoryLimits()` to remove the caps.

### Sandboxing untrusted scripts

For untrusted input, also fence off the file system and trim the builtin surface:

```cpp
manager.setImportsEnabled(false);             // or whitelist:
manager.addImportRoot("/var/app/scripts");    // imports must resolve inside roots

manager.disableBuiltin("print");              // hide I/O builtins
manager.disableBuiltin("srand");              // keep the RNG seed unpredictable
manager.setOutputCallback([](const std::string &s) { /* log safely */ });
```

When import roots are configured, `import` paths must canonicalize inside one of them — attempts
to escape (`import "../../etc/passwd"`) fail at load time. Disabled builtins resolve like
undefined functions, so scripts calling them fail to compile.

### Hot reload and the AST cache

`reloadScriptFile(path, errors)` recompiles one file and atomically swaps in its procedures —
externals, REPL globals, and other files' declarations are preserved; on failure the old
definitions stay loaded. Enable `setAstCacheEnabled(true)` to skip re-lexing/re-parsing files
whose content hasn't changed during repeated loads or reloads.


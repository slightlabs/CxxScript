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
| `clear()` | Reset interpreter state (removes loaded procedures and external bindings) |

See the full [API Reference](api-reference.md) for type signatures.

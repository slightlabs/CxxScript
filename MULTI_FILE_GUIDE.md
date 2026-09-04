# Multi-File Script Support

The scripting system supports loading and executing procedures from multiple `.script` files, enabling code organization and cross-file procedure calls.

## Features

### 1. Loading Multiple Files
```cpp
ScriptManager manager;
std::vector<CompilationError> errors;

manager.loadScriptFile("utils/math.script", errors);
manager.loadScriptFile("utils/string.script", errors);
manager.loadScriptFile("main.script", errors);
```

### 2. Cross-File Procedure Calls
Procedures can call procedures from other loaded files:

**math_utils.script:**
```cpp
int32 add(int32 a, int32 b) {
  return a + b;
}
```

**main_logic.script:**
```cpp
int32 computeSum(int32 x, int32 y) {
  return add(x, y);  // Calls add() from math_utils.script
}
```

### 3. Integration with External C++ Functions
Procedures from multiple files can call registered C++ functions:

```cpp
manager.registerExternalFunction("print", [](const std::vector<Value>& args) {
  std::cout << std::get<std::string>(args[0]) << std::endl;
  return Value();
});
```

Any loaded script file can call `print()`.

### 4. Duplicate Procedure Handling
- Later loaded files overwrite procedures with the same name
- The system tracks which file each procedure came from
- Use `getProcedureInfo()` to check procedure source file

### 5. File Management
```cpp
// Clear all loaded procedures
manager.clear();

// Reload files
manager.loadScriptFile("file1.script", errors);

// Re-register external functions/variables after clear()
```

## API Methods

### ScriptManager
- `loadScriptFile(filename, errors)` - Load procedures from a file
- `hasProcedure(name)` - Check if procedure exists
- `getProcedureNames()` - Get all loaded procedure names
- `getProcedureInfo(name)` - Get procedure details including source file
- `executeProcedure(name, args, result, error)` - Execute a procedure
- `clear()` - Reset interpreter state (loaded procedures and external bindings)

### External Functions
- `registerExternalFunction(name, callback)` - Add C++ function
- `unregisterExternalFunction(name)` - Remove C++ function
- `hasExternalFunction(name)` - Check if function is registered
- External functions are available to all loaded script files

## Example Structure

```
project/
├── scripts/
│   ├── math_utils.script      # Mathematical operations
│   ├── string_utils.script    # String manipulation
│   ├── validators.script      # Input validation
│   └── main.script            # Main logic calling others
└── src/
    └── main.cpp               # C++ application
```

## Test Coverage

The `test_multi_file` test suite validates:
1. Loading multiple files
2. Cross-file procedure calls
3. Integration with external C++ functions
4. Procedure source file tracking
5. Duplicate procedure name handling
6. Clear and reload functionality
7. External function persistence
8. Complex multi-file scenarios
9. Unregister external function impact

Run tests with: `ctest` or `./test_multi_file`

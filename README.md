# CxxScript

[![CI](https://github.com/slightlabs/CxxScript/actions/workflows/ci.yml/badge.svg)](https://github.com/slightlabs/CxxScript/actions/workflows/ci.yml)
[![Deploy Docs](https://github.com/slightlabs/CxxScript/actions/workflows/deploy-docs.yml/badge.svg)](https://github.com/slightlabs/CxxScript/actions/workflows/deploy-docs.yml)
[![Release](https://img.shields.io/github/v/release/slightlabs/CxxScript)](https://github.com/slightlabs/CxxScript/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

CxxScript is a modern C++ scripting engine that lets you write and execute `.script` files with multiple procedures.

📖 **Full documentation and examples:** [slightlabs.github.io/CxxScript](https://slightlabs.github.io/CxxScript/)

## Quick Start

```bash
# Build the project
cmake -S . -B build
cmake --build build

# Run tests
cd build && ctest

# Run example
./build/bin/example_usage
```

## Features

- **Multiple Data Types**: int8, uint8, int16, uint16, int32, uint32, int64, uint64, double, string, bool, and typed arrays of any scalar (e.g., `int32[]`).
- **Arrays Built-ins**: array literals `[1,2,3]`, indexing `arr[0]`, mutation `arr[0] = 5`, `len(arr)`, `push(arr, value)` (returns new length), `pop(arr)` (returns last element, errors on empty).
- **Arithmetic Operators**: +, -, *, /, % with proper precedence (modulo is integer-only; floating point uses +,-,*,/)
- **Bitwise Operators**: &, |, ^, ~, <<, >> (integers only)
- **Logical Operators**: !, &&, || with short-circuit evaluation
- **Control Flow**: if/else, while, for, do-while, switch/case/default, ternary `?:`, break/continue
- **Compound Assignments**: +=, -=, *=, /=
- **Procedure Calls**: Scripts can call other procedures defined in the same or different script files
- **External Function Callbacks**: Call C++ functions from scripts with generic argument passing, bulk registration, and typed helper wrappers
- **External Variables**: Expose host variables to scripts via getters/setters (read/write or read-only helper)
- **Comprehensive Error Reporting**: Compilation and runtime errors with line numbers and procedure names
- **Decoupled Parser**: Parser logic is separated for easy unit testing

## Project Structure

```
/cxxscript/
├── include/              # Public headers (Library API)
│   ├── AST.h, DataTypes.h, Interpreter.h
│   ├── Lexer.h, Parser.h, ScriptManager.h, Token.h
├── src/                  # Implementation files
│   ├── DataTypes.cpp, Interpreter.cpp, Lexer.cpp
│   ├── Parser.cpp, ScriptManager.cpp, Token.cpp
├── tests/                # Test suite
│   ├── test_lexer.cpp, test_parser.cpp, test_interpreter.cpp
│   ├── test_error_handling.cpp, test_comprehensive.cpp
│   ├── test_external_functions.cpp, test_multi_file.cpp
│   ├── test_string_concat.cpp, test_real_world_app.cpp
├── examples/             # Example applications
│   ├── example_usage.cpp, demo_error_detection.cpp
├── scripts/              # Script files and test data
│   ├── example.script, demo_concat.script
│   └── test_files/      # Test script modules
├── docs/                 # MkDocs site source (published to GitHub Pages)
├── .github/workflows/    # CI, auto-tag, release, and docs-deploy pipelines
├── conanfile.py           # Conan 2.x package recipe
├── test_package/          # Conan recipe smoke test
├── build/                # Build outputs (generated)
│   ├── lib/             # libCxxScript.a
│   ├── bin/             # Examples
│   └── tests/           # Test executables
├── CMakeLists.txt       # Build configuration
├── mkdocs.yml           # Documentation site configuration
├── LICENSE              # MIT license
└── Documentation files
    ├── README.md (this file)
    ├── ORGANIZED_STRUCTURE.md   # Detailed structure guide
    ├── API_CHANGES.md           # API documentation
    ├── MULTI_FILE_GUIDE.md      # Multi-file scripting
    └── REAL_WORLD_EXAMPLE.md    # E-commerce example
```

See [ORGANIZED_STRUCTURE.md](ORGANIZED_STRUCTURE.md) for detailed structure documentation.

## Script Syntax

### Procedure Definition

```cpp
returnType procedureName(type1 param1, type2 param2) {
    // statements
    return value;
}
```

### String Literals and Escape Sequences

Strings support the following escape sequences:

```cpp
string examples() {
    string quote = "He said \"Hello\"";           // \" - Quote
    string path = "C:\\Users\\Name";               // \\ - Backslash
    string multiline = "Line 1\nLine 2";          // \n - Newline
    string tabbed = "Col1\tCol2\tCol3";           // \t - Tab
    string cr = "Text\rOverwrite";                // \r - Carriage return
    string nullTerm = "Text\0More";               // \0 - Null character
    
    // Unknown escapes are preserved
    string unknown = "Keep \\x as-is";            // \x - Unknown (kept as \x)
    
    return quote;
}
```

Supported escape sequences:
- `\"` - Double quote
- `\\` - Backslash
- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return
- `\0` - Null character

### Example Script

#### Arrays

```cpp
int32 arraysDemo(int32 x) {
    int32[] nums = [1, 2, x];
    push(nums, 10);          // nums = [1,2,x,10]
    int32 last = pop(nums);  // last = 10, nums = [1,2,x]
    nums[0] = nums[0] + 5;   // mutate in-place
    return nums[0] + len(nums); // (1+5) + 3 = 9 when x = 3
}
```

```cpp
bool calculate(int32 arg1, int32 arg2) {
    int32 var1 = arg1 + 56;
    int32 var2 = arg2 / 34;
    int32 total = var1 + var2;
    
    if (total > 43) {
        return true;
    }
    
    return false;
}

int32 factorial(int32 n) {
    int32 result = 1;
    for (int32 i = 1; i <= n; i += 1) {
        result *= i;
    }
    return result;
}
```

### Bitwise and Logical Examples

```cpp
// Bitwise operations (integers only)
int32 bitwiseDemo(int32 a, int32 b) {
    int32 andVal = a & b;
    int32 orVal  = a | b;
    int32 xorVal = a ^ b;
    int32 shlVal = a << 1;
    int32 shrVal = b >> 1;
    int32 notVal = ~a;
    return andVal + orVal + xorVal + shlVal + shrVal + notVal;
}

// Logical operators short-circuit
bool shortCircuitDemo(bool x, bool y) {
    // right side is skipped when left decides the outcome
    return (x && expensiveTrue()) || (y || expensiveFalse());
}

// These could be external functions registered from C++
// to observe call counts/side effects.
bool expensiveTrue()  { return true; }
bool expensiveFalse() { return false; }
```

## Usage in C++ Application

```cpp
#include "ScriptManager.h"

using namespace Script;

// 1. Create script manager
ScriptManager scriptManager;

// 2. (Optional) Register external function(s)
scriptManager.registerExternalFunction(
    "myFunction",
    [](const std::vector<Value>& args) -> Value {
        // Handle external function call
        return static_cast<int32_t>(42 + std::get<int32_t>(args[0]));
    }
);

// Bulk-register a couple of helpers (initializer_list is supported)
scriptManager.registerExternalFunctions({
    {"add", [](const std::vector<Value> &args) {
        return static_cast<int32_t>(ValueHelper::toInt64(args[0]) +
                                    ValueHelper::toInt64(args[1]));
    }},
    {"triple", [](const std::vector<Value> &args) {
        return static_cast<int32_t>(ValueHelper::toInt64(args[0]) * 3);
    }},
});

// Typed helper (binary int32)
scriptManager.registerExternalFunctionBinary<int32_t, int32_t, int32_t>(
    "mul", [](int32_t a, int32_t b) { return a * b; });

// 3. (Optional) Register external variable(s)
int32_t hostValue = 10;
scriptManager.registerExternalVariable(
    "sharedValue",
    [&]() -> Value { return static_cast<int32_t>(hostValue); },
    [&](const Value& v) { hostValue = std::get<int32_t>(v); }
);

// Read-only variable helper
int32_t readonlyMetric = 7;
scriptManager.registerExternalVariableReadOnly(
    "metric", [&]() -> Value { return static_cast<int32_t>(readonlyMetric); });

// 4. Load script file
std::vector<CompilationError> errors;
if (!scriptManager.loadScriptFile("example.script", errors)) {
    // Handle compilation errors
    for (const auto& error : errors) {
        std::cout << error.toString() << std::endl;
    }
    return;
}

// 5. Execute a procedure
std::vector<Value> arguments = {
    static_cast<int32_t>(10),
    static_cast<int32_t>(20)
};

Value returnValue;
std::string errorMessage;

if (scriptManager.executeProcedure("calculate", arguments, returnValue, errorMessage)) {
    bool result = std::get<bool>(returnValue);
    std::cout << "Result: " << result << std::endl;
} else {
    std::cout << "Error: " << errorMessage << std::endl;
}
```

## API Reference

### ScriptManager

Main class for managing scripts:

- `loadScriptFile(filename, errors)` - Load and compile a script file
- `loadScriptSource(source, filename, errors)` - Load script from string
- `checkScript(filename, errors)` - Check compilation without loading
- `executeProcedure(name, args, returnValue, errorMsg)` - Execute a procedure
- `hasProcedure(name)` - Check if procedure exists
- `getProcedureNames()` - Get list of all loaded procedures
- `getProcedureInfo(name, info)` - Get procedure signature
- `registerExternalFunction(name, callback)` / `registerExternalFunctions({...})` / `unregisterExternalFunction(name)` - Bind or remove host callbacks (bulk registration supported)
- Typed helpers: `registerExternalFunctionUnary` / `registerExternalFunctionBinary` for common primitive types
- `registerExternalVariable(name, getter, setter)` / `registerExternalVariableReadOnly(name, getter)` / `unregisterExternalVariable(name)` - Expose host variables (read/write or read-only)
- `clear()` - Clear all loaded scripts

### External Function Callback

```cpp
using ExternalFunctionCallback = std::function<Value(
    const std::vector<Value>& arguments
)>;
```

Your C++ application receives the argument values and returns a `Value`.

### External Variables

```cpp
using ExternalVariableGetter = std::function<Value()>;
using ExternalVariableSetter = std::function<void(const Value&)>; // optional
```

- Getter is required; setter is optional for read-only values.
- Compound assignments (`+=`, etc.) require both getter and setter.

Return the result as a `Value`.

## External Integration Examples

### External Functions

```cpp
ScriptManager manager;

// Register multiple functions
manager.registerExternalFunction("addOne", [](const std::vector<Value>& args) {
    return static_cast<int32_t>(std::get<int32_t>(args[0]) + 1);
});

manager.registerExternalFunction("greet", [](const std::vector<Value>& args) {
    return std::string("Hello, ") + std::get<std::string>(args[0]);
});

// Script can call them directly
// script:
//   int32 run(int32 x) {
//     string msg = greet("world"); // -> "Hello, world"
//     return addOne(x);             // -> x + 1
//   }
```

Notes:
- Arguments are passed as `Value` and must be unwrapped with `std::get<T>`. Use the script's expected type.
- You can overwrite a function by re-registering the same name.
- Return any supported scalar or array `Value`.

### External Variables

```cpp
int32_t health = 100;
bool isGodMode = false;

ScriptManager manager;

// Read/write variable
manager.registerExternalVariable(
    "health",
    [&]() -> Value { return static_cast<int32_t>(health); },
    [&](const Value& v) { health = std::get<int32_t>(v); }
);

// Read-only variable
manager.registerExternalVariable(
    "godMode",
    [&]() -> Value { return isGodMode; }
);

// script:
//   void damage(int32 amount) {
//     if (!godMode) { health -= amount; }
//   }
//   int32 heal(int32 amount) {
//     health += amount;
//     return health;
//   }
```

Notes:
- Setter is optional; without it the variable is read-only and assignments will raise a runtime error.
- Compound assignments (`+=`, `-=`, `*=`, `/=`) need both getter and setter so the interpreter can read-modify-write.
- Types must match what the script expects; use `std::get<T>` and return the proper `Value` to avoid runtime conversion errors.

## Building and Testing

### Using CMake (Recommended):

```bash
# Create build directory
mkdir build
cd build

# Configure
cmake ..

# Build
cmake --build .

# Run tests
ctest --output-on-failure
# Or use the custom target:
cmake --build . --target run_tests

# Run example
cmake --build . --target run_example
# Or directly:
./example_usage
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `CXXSCRIPT_BUILD_TESTS` | `ON` | Build the GoogleTest suite (fetched via `FetchContent`) |
| `CXXSCRIPT_BUILD_EXAMPLES` | `ON` | Build the example/demo executables |

Turn both off when consuming CxxScript as a dependency (e.g. `add_subdirectory`, Conan) to skip
GoogleTest and the demo binaries:

```bash
cmake -S . -B build -DCXXSCRIPT_BUILD_TESTS=OFF -DCXXSCRIPT_BUILD_EXAMPLES=OFF
```

## Installation (development use)

Install to standard locations (headers + static lib + scripts + CMake package config):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
sudo cmake --install build
```

Key install locations (GNUInstallDirs):
- Headers: `<prefix>/include/CxxScript`
- Library: `<prefix>/lib/libCxxScript.a`
- Scripts: `<prefix>/share/CxxScript/scripts`
- CMake package: `<prefix>/lib/cmake/CxxScript/CxxScriptConfig.cmake`

Consume from another CMake project after install:

```cmake
find_package(CxxScript CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE CxxScript::CxxScript)
```

Custom prefix for devices:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/opt/cxxscript -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
sudo cmake --install build
```

## Packaging (CPack)

Generate DEB/RPM/tarball artifacts from the build tree:

```bash
cmake --build build --target package
# or: (from build dir) cpack -G DEB   # alternatives: RPM, TGZ
```

Resulting packages contain the installed headers, library, scripts, and CMake config for downstream `find_package` usage.

## Conan

CxxScript also ships a Conan 2.x recipe (`conanfile.py`):

```bash
pip install "conan>=2.0"
conan profile detect --force
conan create . --build=missing
```

Consume it from another project:

```ini
# conanfile.txt
[requires]
cxxscript/1.0.0

[generators]
CMakeDeps
CMakeToolchain
```

See [Building & Packaging → Conan](https://slightlabs.github.io/CxxScript/building/conan/) for details.

### Manual compilation (if needed):

```bash
# Compile implementation files
g++ -std=c++17 -c Token.cpp DataTypes.cpp Lexer.cpp Parser.cpp Interpreter.cpp ScriptManager.cpp

# Build test executables
g++ -std=c++17 test_lexer.cpp Token.o Lexer.o -o test_lexer
g++ -std=c++17 test_parser.cpp Token.o Lexer.o Parser.o -o test_parser
g++ -std=c++17 test_interpreter.cpp Token.o DataTypes.o Lexer.o Parser.o Interpreter.o ScriptManager.o -o test_interpreter

# Build example
g++ -std=c++17 example_usage.cpp Token.o DataTypes.o Lexer.o Parser.o Interpreter.o ScriptManager.o -o example_usage

# Run tests
./test_lexer
./test_parser
./test_interpreter
./example_usage
```

## Error Handling

### Compilation Errors

Errors detected during parsing include:
- Syntax errors with line and column numbers
- Type mismatches
- Undefined types
- Duplicate procedure names

### Runtime Errors

Errors during execution include:
- Undefined variables
- Division by zero
- Type conversion errors
- Undefined procedure calls
- Line number and procedure name included

## Supported Operations

### Arithmetic
- Addition: `+`
- Subtraction: `-`
- Multiplication: `*`
- Division: `/`
- Modulo: `%`

### Comparison
- Equal: `==`
- Not equal: `!=`
- Less than: `<`
- Greater than: `>`
- Less or equal: `<=`
- Greater or equal: `>=`

### Logical
- AND: `&&`
- OR: `||`
- NOT: `!`

### Assignment
- Assign: `=`
- Plus assign: `+=`
- Minus assign: `-=`
- Multiply assign: `*=`
- Divide assign: `/=`

## Comments

```cpp
// Single-line comment

/*
 * Multi-line
 * comment
 */
```

## License

Licensed under the [MIT License](LICENSE).

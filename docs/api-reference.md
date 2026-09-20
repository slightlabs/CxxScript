# API Reference

## `Script::Value`

```cpp
using Value = std::variant<
    char, int8_t, uint8_t, int16_t, uint16_t,
    int32_t, uint32_t, int64_t, uint64_t,
    float, double, std::string, bool,
    ArrayPtr, MapPtr, StructPtr, FuncPtr
>;
```

A `Value` holds any scalar supported by the language, or a shared pointer to a typed array
(`ArrayPtr`), map (`MapPtr`), struct (`StructPtr`), or function (`FuncPtr` — lambdas, procedure
references, and bound methods). Container values are shared: copying the `Value` shares the
underlying elements.

## `Script::TypeInfo`

Describes a static or runtime type. `baseType` names the scalar family; composite types carry
recursive metadata:

| Member | Meaning |
|---|---|
| `arrayElem` | Element type of an array (`int32[][]` → `arrayElem` → `int32[]`) |
| `mapValueType` | Value type of a `map<K, V>` |
| `structName` | Name when the type is a declared `struct` |
| `isFunction`, `paramTypes`, `retType` | Function signature (`fn(int32) -> int32`) |
| `fnOpaque` | An overloaded-procedure reference whose signature resolves at call time |
| `isAuto` | The `auto` placeholder |

Constructors: `TypeInfo::arrayOf`, `mapOf`, `structOf`, `functionOf`, `opaqueFunction`,
`autoType`.

## `Script::ValueHelper`

| Method | Purpose |
|---|---|
| `ValueHelper::getType(value)` | Returns the `TypeInfo` of a `Value` |
| `ValueHelper::typeToString(type)` | Human-readable type name, e.g. `"int32"`, `"map<string, int32>"` |
| `ValueHelper::stringToType(str)` | Parses a type name back into `TypeInfo` |
| `ValueHelper::toInt64(value)` | Converts any integer/bool `Value` to `int64_t` |
| `ValueHelper::toDouble(value)` | Converts a numeric `Value` to `double` |
| `ValueHelper::toBool(value)` | Converts a `Value` to `bool` |
| `ValueHelper::toString(value)` | Converts any `Value` to its `string` representation (depth-guarded on cycles) |
| `ValueHelper::equals(a, b)` | Deep equality for containers and functions |

## `Script::CompilationError`

```cpp
struct CompilationError {
    std::string message;
    std::string filename;
    std::string procedureName;
    int line;
    int column;
    bool isWarning;    // warnings don't fail compilation

    std::string toString() const;
};
```

Reported for both compile-time (`loadScriptFile`/`loadScriptSource`) and check-only
(`checkScript`/`checkScriptSource`) calls. Diagnostics like unused variables and ambiguous
overloads come back with `isWarning == true`.

## `Script::ScriptManager`

```cpp
class ScriptManager {
public:
    ScriptManager();
    ~ScriptManager();

    bool loadScriptFile(const std::string &filename,
                        std::vector<CompilationError> &errors);
    bool loadScriptSource(const std::string &source, const std::string &filename,
                          std::vector<CompilationError> &errors);

    bool checkScript(const std::string &filename,
                     std::vector<CompilationError> &errors);
    bool checkScriptSource(const std::string &source, const std::string &filename,
                           std::vector<CompilationError> &errors);

    // Recompile a file and atomically swap in its procedures (hot reload).
    // Externals, REPL globals, and other files' procedures are preserved;
    // on compile failure the old definitions stay loaded.
    bool reloadScriptFile(const std::string &filename,
                          std::vector<CompilationError> &errors);

    bool executeProcedure(const std::string &procedureName,
                          const std::vector<Value> &arguments, Value &returnValue,
                          std::string &errorMessage);

    // Evaluate top-level statements (REPL). Declared variables persist as
    // globals; a top-level `return <expr>` yields its value.
    bool evaluateSnippet(const std::string &source, const std::string &name,
                         Value &returnValue, std::string &errorMessage);

    bool hasProcedure(const std::string &name) const;
    std::vector<std::string> getProcedureNames() const;

    struct ProcedureInfo {
        std::string name;
        TypeInfo returnType;
        std::vector<Parameter> parameters;
        std::string filename;
    };
    bool getProcedureInfo(const std::string &name, ProcedureInfo &info) const;

    void registerExternalFunction(const std::string &name, ExternalFunctionCallback callback);
    void registerExternalFunctions(const std::vector<ExternalBinding> &bindings);
    void registerExternalFunctions(std::initializer_list<ExternalBinding> bindings);
    void unregisterExternalFunction(const std::string &name);
    bool hasExternalFunction(const std::string &name) const;

    void registerExternalVariable(const std::string &name,
                                  ExternalVariableGetter getter,
                                  ExternalVariableSetter setter = nullptr);
    void registerExternalVariableReadOnly(const std::string &name,
                                           ExternalVariableGetter getter);
    void unregisterExternalVariable(const std::string &name);
    bool hasExternalVariable(const std::string &name) const;

    // Typed external functions — arity and argument types deduced from the
    // signature; see "Typed helpers" below.
    template <typename Ret, typename... Args>
    void registerExternalFunction(const std::string &name, std::function<Ret(Args...)> fn);

    void clear();
};
```

### Execution guardrails

All limits are optional and disabled by default (`0` means unlimited). Violations raise a
**fatal** runtime error that script `try`/`catch` cannot suppress.

```cpp
void setExecutionLimits(size_t maxCallDepth, size_t maxSteps);
void clearExecutionLimits();
void setMemoryLimits(size_t maxArraySize, size_t maxStringLength,
                     size_t maxAllocations);
void clearMemoryLimits();
```

`maxArraySize` caps elements per array, `maxStringLength` caps produced strings, and
`maxAllocations` caps total array allocations per top-level execution.

### Output and debugging

```cpp
// Redirect print()/println() output (nullptr restores stdout).
void setOutputCallback(Interpreter::OutputCallback cb);

// Invoked before each statement executes.
struct Interpreter::DebugContext {
    std::string filename;
    int line, column;
    std::string procedure;
    std::unordered_map<std::string, Value> variables; // visible-locals snapshot
    size_t callDepth;
    std::vector<std::string> callStack;               // innermost last
};
void setDebugHook(Interpreter::DebugHook cb);
```

The `cxxscript debug` command is built on this hook — see [CLI](cli.md#debug).

### Sandbox controls

```cpp
void addImportRoot(const std::string &directory);  // imports must resolve inside a root
void clearImportRoots();                           // empty = anywhere (default)
void setImportsEnabled(bool enabled);              // master switch for `import`

void disableBuiltin(const std::string &name);      // e.g. "print", "srand"
void enableBuiltin(const std::string &name);
bool isBuiltinEnabled(const std::string &name) const;
```

A disabled builtin resolves like an undefined function — calls fail at compile time (or
runtime if reached through an opaque path).

### Parsed-AST cache

```cpp
void setAstCacheEnabled(bool enabled);   // default off
bool isAstCacheEnabled() const;
size_t astCacheSize() const;
void clearAstCache();
```

When enabled, successfully parsed files are cached by canonical path + source hash, so
recompiles (`reloadScriptFile`, repeated `loadScriptFile`) skip lexing/parsing. Semantic
validation still runs on every compile.

## `Script::Formatter`

```cpp
std::string Formatter::format(const std::string &source,
                              const std::string &filename = "<fmt>");
```

Canonical pretty-printer — two-space indentation, normalized spacing/braces, comments
re-emitted in source order. Throws `ParseError` on unparsable input. The `cxxscript fmt`
command wraps it; note that numeric literal spellings normalize to decimal.

## Callback signatures

```cpp
using ExternalFunctionCallback = std::function<Value(const std::vector<Value> &arguments)>;
using ExternalVariableGetter   = std::function<Value()>;
using ExternalVariableSetter   = std::function<void(const Value &)>; // optional
```

### Typed helpers

`registerExternalFunction` accepts `std::function`, free functions, and lambdas with any of
these argument/return types: all integer widths, `float`, `double`, `bool`, `char`,
`std::string`, `ArrayPtr`, and `void` returns. Arity is enforced; arguments convert with the
same rules as procedure parameters.

```cpp
manager.registerExternalFunction("add",
    std::function<int32_t(int32_t, int32_t)>(
        [](int32_t a, int32_t b) { return a + b; }));
manager.registerExternalFunction("inc", [](int64_t n) { return n + 1; });
```

For the full headers, see
[`include/ScriptManager.h`](https://github.com/slightlabs/CxxScript/blob/main/include/ScriptManager.h),
[`include/Interpreter.h`](https://github.com/slightlabs/CxxScript/blob/main/include/Interpreter.h),
and [`include/DataTypes.h`](https://github.com/slightlabs/CxxScript/blob/main/include/DataTypes.h).

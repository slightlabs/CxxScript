# API Reference

## `Script::Value`

```cpp
using Value = std::variant<
    int8_t, uint8_t, int16_t, uint16_t,
    int32_t, uint32_t, int64_t, uint64_t,
    double, std::string, bool,
    ArrayPtr
>;
```

A `Value` holds any scalar supported by the language, or a shared, typed array (`ArrayPtr`).

## `Script::ValueHelper`

| Method | Purpose |
|---|---|
| `ValueHelper::getType(value)` | Returns the `TypeInfo` of a `Value` |
| `ValueHelper::typeToString(type)` | Human-readable type name, e.g. `"int32"`, `"string[]"` |
| `ValueHelper::stringToType(str)` | Parses a type name back into `TypeInfo` |
| `ValueHelper::toInt64(value)` | Converts any integer/bool `Value` to `int64_t` |
| `ValueHelper::toDouble(value)` | Converts a numeric `Value` to `double` |
| `ValueHelper::toBool(value)` | Converts a `Value` to `bool` |
| `ValueHelper::toString(value)` | Converts any `Value` to its `string` representation |

## `Script::CompilationError`

```cpp
struct CompilationError {
    std::string message;
    std::string filename;
    std::string procedureName;
    int line;
    int column;

    std::string toString() const;
};
```

Reported for both compile-time (`loadScriptFile`/`loadScriptSource`) and check-only
(`checkScript`/`checkScriptSource`) calls.

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

    bool executeProcedure(const std::string &procedureName,
                          const std::vector<Value> &arguments, Value &returnValue,
                          std::string &errorMessage);

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

    template <typename Ret, typename Arg>
    void registerExternalFunctionUnary(const std::string &name, std::function<Ret(Arg)> fn);

    template <typename Ret, typename Arg1, typename Arg2>
    void registerExternalFunctionBinary(const std::string &name,
                                        std::function<Ret(Arg1, Arg2)> fn);

    void clear();
};
```

Typed helpers (`registerExternalFunctionUnary`/`Binary`) support `int32_t`, `double`, `bool`, and
`std::string` for both arguments and return type.

## Callback signatures

```cpp
using ExternalFunctionCallback = std::function<Value(const std::vector<Value> &arguments)>;
using ExternalVariableGetter   = std::function<Value()>;
using ExternalVariableSetter   = std::function<void(const Value &)>; // optional
```

For the full header, see
[`include/ScriptManager.h`](https://github.com/slightlabs/CxxScript/blob/main/include/ScriptManager.h)
and [`include/DataTypes.h`](https://github.com/slightlabs/CxxScript/blob/main/include/DataTypes.h).

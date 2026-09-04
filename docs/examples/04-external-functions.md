# Example 4: External Functions

Scripts can call native C++ functions that the host registers before loading the script.

## Registering functions

```cpp
ScriptManager manager;

manager.registerExternalFunction("addOne", [](const std::vector<Value> &args) {
    return static_cast<int32_t>(std::get<int32_t>(args[0]) + 1);
});

manager.registerExternalFunction("greet", [](const std::vector<Value> &args) {
    return std::string("Hello, ") + std::get<std::string>(args[0]);
});

// Bulk registration via initializer list
manager.registerExternalFunctions({
    {"add", [](const std::vector<Value> &args) {
        return static_cast<int32_t>(ValueHelper::toInt64(args[0]) +
                                    ValueHelper::toInt64(args[1]));
    }},
    {"triple", [](const std::vector<Value> &args) {
        return static_cast<int32_t>(ValueHelper::toInt64(args[0]) * 3);
    }},
});

// Typed helper for common signatures (binary int32 -> int32)
manager.registerExternalFunctionBinary<int32_t, int32_t, int32_t>(
    "mul", [](int32_t a, int32_t b) { return a * b; });
```

## Calling them from a script

```cpp
int32 run(int32 x) {
    string msg = greet("world"); // -> "Hello, world"
    return addOne(x);            // -> x + 1
}
```

## Notes

- Arguments are passed as `Value` and must be unwrapped with `std::get<T>` or `ValueHelper`
  matching the type the script passes.
- Re-registering a name overwrites the previous callback.
- A callback can return any supported scalar or array `Value`.
- `registerExternalFunctions(...)` accepts either a `std::vector<ExternalBinding>` or an
  `std::initializer_list<ExternalBinding>` for bulk registration.

Next: [Example 5: External Variables](05-external-variables.md).

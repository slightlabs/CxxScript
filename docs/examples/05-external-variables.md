# Example 5: External Variables

External variables expose host state to scripts through a getter (required) and an optional
setter.

## Registering variables

```cpp
int32_t health = 100;
bool isGodMode = false;

ScriptManager manager;

// Read/write variable
manager.registerExternalVariable(
    "health",
    [&]() -> Value { return static_cast<int32_t>(health); },
    [&](const Value &v) { health = std::get<int32_t>(v); }
);

// Read-only variable (no setter)
manager.registerExternalVariableReadOnly(
    "godMode", [&]() -> Value { return isGodMode; });
```

## Using them from a script

```cpp
void damage(int32 amount) {
    if (!godMode) { health -= amount; }
}

int32 heal(int32 amount) {
    health += amount;
    return health;
}
```

## Notes

- Without a setter, the variable is read-only; assigning to it raises a runtime error.
- Compound assignments (`+=`, `-=`, `*=`, `/=`) require **both** a getter and a setter, since the
  interpreter performs a read-modify-write.
- Types must match what the script expects — use `std::get<T>` on read and return a matching
  `Value` on write to avoid runtime conversion errors.

Next: [Example 6: Multi-File Scripts](06-multi-file.md).

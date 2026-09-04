# Example 6: Multi-File Scripts

Large scripts are easier to maintain when split across files. CxxScript lets a single
`ScriptManager` load several `.script` files whose procedures can call each other.

## Layout

```text
scripts/test_files/
├── math_utils.script      # add(), square()
├── string_utils.script    # concat(), greet(), formatNumber()
└── main_logic.script      # calls into the two files above
```

`math_utils.script`:

```cpp
int32 add(int32 a, int32 b) { return a + b; }
int32 square(int32 n) { return n * n; }
```

`main_logic.script`:

```cpp
int32 computeSum(int32 x, int32 y) {
    // Calls add() from math_utils.script
    return add(x, y);
}

int32 computeSquareSum(int32 a, int32 b) {
    int32 sq1 = square(a);
    int32 sq2 = square(b);
    return add(sq1, sq2);
}

string makeGreeting(string firstName, string lastName) {
    // Calls concat() and greet() from string_utils.script
    string fullName = concat(firstName, concat(" ", lastName));
    return greet(fullName);
}
```

## Loading multiple files

```cpp
ScriptManager manager;
std::vector<CompilationError> errors;

manager.loadScriptFile("scripts/test_files/math_utils.script", errors);
manager.loadScriptFile("scripts/test_files/string_utils.script", errors);
manager.loadScriptFile("scripts/test_files/main_logic.script", errors);

Value result;
std::string errorMessage;
manager.executeProcedure("computeSquareSum",
                          {static_cast<int32_t>(3), static_cast<int32_t>(4)},
                          result, errorMessage);
// std::get<int32_t>(result) == 25  (3*3 + 4*4)
```

## Notes

- External functions registered on the `ScriptManager` are available to **every** loaded file.
- If two files define a procedure with the same name, the most recently loaded file wins; use
  `getProcedureInfo()` to check which file a procedure came from.
- `manager.clear()` removes all loaded procedures but keeps registered external functions and
  variables, so files can be reloaded without re-registering the host bindings.

Next: [Example 7: Real-World App](07-real-world.md).

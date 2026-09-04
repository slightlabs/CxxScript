# Example 2: Control Flow & Loops

Builds on [Example 1](01-hello-world.md) by combining `for`, `while`, `if/else`, and `switch` into
a small set of procedures.

```cpp
// scripts/example.script

int32 factorial(int32 n) {
    int32 result = 1;
    for (int32 i = 1; i <= n; i += 1) {
        result *= i;
    }
    return result;
}

bool isPrime(int32 n) {
    if (n <= 1) {
        return false;
    }
    for (int32 i = 2; i < n; i += 1) {
        if (n % i == 0) {
            return false;
        }
    }
    return true;
}

int32 sumRange(int32 n) {
    int32 sum = 0;
    int32 i = 1;
    while (i <= n) {
        sum += i;
        i += 1;
    }
    return sum;
}

int32 classify(int32 v) {
    int32 out = 0;
    switch (v) {
        case 1: out = 10; break;
        case 2: out = 20; break;
        default: out = 99; break;
    }
    return out;
}
```

## Running it

```cpp
ScriptManager manager;
std::vector<CompilationError> errors;
manager.loadScriptFile("scripts/example.script", errors);

Value result;
std::string errorMessage;

manager.executeProcedure("factorial", {static_cast<int32_t>(5)}, result, errorMessage);
// std::get<int32_t>(result) == 120

manager.executeProcedure("isPrime", {static_cast<int32_t>(17)}, result, errorMessage);
// std::get<bool>(result) == true

manager.executeProcedure("sumRange", {static_cast<int32_t>(10)}, result, errorMessage);
// std::get<int32_t>(result) == 55
```

See the full syntax reference in [Control Flow](../language/control-flow.md).

Next: [Example 3: Arrays](03-arrays.md).

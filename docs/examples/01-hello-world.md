# Example 1: Hello World

The simplest possible CxxScript program: one procedure that adds two numbers.

## The script

`hello.script`:

```cpp
int32 add(int32 a, int32 b) {
    return a + b;
}
```

## Running it from C++

```cpp
#include "ScriptManager.h"
#include <iostream>

using namespace Script;

int main() {
    ScriptManager manager;
    std::vector<CompilationError> errors;

    if (!manager.loadScriptFile("hello.script", errors)) {
        for (const auto &error : errors) {
            std::cerr << error.toString() << std::endl;
        }
        return 1;
    }

    Value result;
    std::string errorMessage;
    std::vector<Value> args = {static_cast<int32_t>(2), static_cast<int32_t>(3)};

    if (manager.executeProcedure("add", args, result, errorMessage)) {
        std::cout << "add(2, 3) = " << std::get<int32_t>(result) << std::endl;
    } else {
        std::cerr << "Error: " << errorMessage << std::endl;
    }
}
```

Output:

```text
add(2, 3) = 5
```

## A slightly bigger "hello"

Strings, concatenation, and a `bool` return type:

```cpp
string greet(string name) {
    string greeting = "Hello, ";
    greeting += name;
    greeting += "!";
    return greeting;
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
```

Next: [Example 2: Control Flow & Loops](02-control-flow.md).

# Control Flow

## `if` / `else`

```cpp
bool calculate(int32 arg1, int32 arg2) {
    int32 total = arg1 + arg2;
    if (total > 43) {
        return true;
    }
    return false;
}
```

## `while`

```cpp
int32 sumRange(int32 n) {
    int32 sum = 0;
    int32 i = 1;
    while (i <= n) {
        sum += i;
        i += 1;
    }
    return sum;
}
```

## `for`

```cpp
int32 factorial(int32 n) {
    int32 result = 1;
    for (int32 i = 1; i <= n; i += 1) {
        result *= i;
    }
    return result;
}
```

## `do-while`

```cpp
int32 atLeastOnce(int32 n) {
    int32 i = n;
    do { i += 1; } while (i < 0);
    return i;
}
```

## `for` each (range-for)

`for (T x : collection)` iterates arrays, strings (yielding `char`s), and maps (yielding keys
in sorted order). Use `auto` or `const T` in place of the type. Scalar loop variables are copies
— use a classic `for` loop with an index to write back — while container elements keep their
shared storage:

```cpp
int32 sumAll(int32[] nums) {
    int32 total = 0;
    for (int32 n : nums) { total += n; }
    return total;
}

int32 countChars(string s) {
    int32 count = 0;
    for (char c : s) { if (c != ' ') { count += 1; } }
    return count;
}
```

```cpp
int32 sumMap(map<string, int32> m) {
    int32 total = 0;
    for (string k : m) { total += m[k]; } // iterates keys; keys(m) is equivalent
    return total;
}
```

## `break` / `continue`

```cpp
int32 sumSkippingEvens(int32 n) {
    int32 total = 0;
    for (int32 i = 0; i < n; i += 1) {
        if (i % 2 == 0) { continue; }
        if (i == 7) { break; }
        total += i;
    }
    return total;
}
```

## `switch` / `case` / `default`

`switch` supports fallthrough between cases until a `break` is reached, just like C++.

```cpp
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

Fallthrough example — cases without `break` accumulate into subsequent cases:

```cpp
int32 fallthroughDemo(int32 v) {
    int32 out = 0;
    switch (v) {
        case 1: out += 1;
        case 2: out += 2;
        case 3: out += 3; break;
        default: out = -1;
    }
    return out;
}
```

## Ternary

```cpp
int32 maxOf(int32 a, int32 b) { return a > b ? a : b; }
```

## `try` / `catch` / `finally` / `throw`

`throw` raises a script exception carrying any `Value`. A `try` block takes at most **one**
`catch` clause and an optional `finally`:

- `catch { ... }` — swallows the exception without binding it
- `catch (e) { ... }` — binds the thrown value as-is (any type)
- `catch (T e) { ... }` — converts the thrown value to type `T` before binding; a failed
  conversion is itself a runtime error

`finally` always runs — on normal completion, after a catch, when an exception propagates
uncaught, and even when the block exits via `return`, `break`, or `continue`.

```cpp
int32 parseOr(string s, int32 fallback) {
    try {
        int32 v = parseInt(s);      // parseInt throws on bad input
        if (v < 0) { throw "negative not allowed"; }
        return v;
    } catch (string msg) {          // binds the thrown message
        println("caught: " + msg);
        return fallback;
    } finally {
        // runs on every path: normal, caught, and uncaught rethrow
    }
}
```

Rules:

- A `try` must have at least one of `catch`/`finally`; `try { } finally { }` is legal and
  rethrows after the finally block runs.
- Ordinary runtime errors — out-of-bounds access, `error()`, `assert()` failures — are also
  delivered through `catch`, bound as a `string` message.
- An uncaught exception propagates to the caller and, at top level, surfaces to the host as
  `Uncaught exception: ...`.
- **Fatal errors are not catchable**: runtime guardrail violations (call-depth, step-count,
  and allocation limits) bypass `catch` so a hostile script cannot suppress them. `finally`
  still runs — though when the *step* budget is the limit that tripped, the `finally` block
  itself may run out of steps before finishing.
- To rethrow, use `throw e` inside the catch block.

```cpp
int32 filter(string s) {
    try {
        return parseInt(s);         // parse errors arrive as a string message
    } catch (e) {
        if (typeof(e) != "string") { throw e; } // rethrow unexpected values
        return -1;
    }
}
```

Ready to see these combined into runnable examples? Continue to
[Example 2: Control Flow & Loops](../examples/02-control-flow.md).

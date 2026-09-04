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

Ready to see these combined into runnable examples? Continue to
[Example 2: Control Flow & Loops](../examples/02-control-flow.md).

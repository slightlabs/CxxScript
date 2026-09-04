# Operators

## Arithmetic

`+`, `-`, `*`, `/`, `%` with standard precedence. `%` (modulo) is integer-only; `+ - * /` also work
on `double`.

```cpp
int32 remainder(int32 a, int32 b) { return a % b; }
```

## Bitwise (integers only)

`&`, `|`, `^`, `~`, `<<`, `>>`

```cpp
int32 bitwiseDemo(int32 a, int32 b) {
    int32 andVal = a & b;
    int32 orVal  = a | b;
    int32 xorVal = a ^ b;
    int32 shlVal = a << 1;
    int32 shrVal = b >> 1;
    int32 notVal = ~a;
    return andVal + orVal + xorVal + shlVal + shrVal + notVal;
}
```

## Logical (short-circuit)

`!`, `&&`, `||`

```cpp
bool shortCircuitDemo(bool x, bool y) {
    // The right-hand side is skipped once the left side decides the outcome.
    return (x && expensiveTrue()) || (y || expensiveFalse());
}
```

## Assignment

`=`, and compound assignments `+=`, `-=`, `*=`, `/=`.

```cpp
int32 accumulate(int32 n) {
    int32 total = 0;
    for (int32 i = 1; i <= n; i += 1) {
        total += i;
    }
    return total;
}
```

!!! note
    Compound assignment on an [external variable](../examples/05-external-variables.md) requires
    both a getter and a setter to be registered, since the interpreter needs to read-modify-write.

## Ternary

```cpp
int32 maxOf(int32 a, int32 b) {
    return a > b ? a : b;
}
```

Next: [Control Flow](control-flow.md).

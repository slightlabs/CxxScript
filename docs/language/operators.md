# Operators

## Arithmetic

`+`, `-`, `*`, `/`, `%` with standard precedence. `%` uses `fmod` semantics on `float`/`double`
(`7.5 % 2.0` → `1.5`) and truncating remainder on integers. Mixing `float` and `double` promotes
to `double`. `char` participates in arithmetic like a small integer (its code point), and `+`
with a `char` and `string` concatenates the literal character.

`+` also concatenates strings (`"a" + "b"`) and — with a string on either side — stringifies the
other operand (`"n=" + 42`).

```cpp
int32 remainder(int32 a, int32 b) { return a % b; }
double frac(double x) { return x % 1.0; }
```

### Increment / decrement

`++` and `--` work as both prefix and postfix on any mutable integer/`char`/`float`/`double`
variable:

```cpp
int32 ticks(int32 n) {
    int32 i = 0;
    while (++i <= n) { /* prefix: value already incremented */ }
    return i;
}
```

## Comparison and equality

`==`, `!=`, `<`, `<=`, `>`, `>=` work on numbers, `char`, `string`, `bool` (equality only), and
containers:

- **Arrays** compare element-wise; ordering is lexicographic (`[1, 2] < [1, 3]`; a strict prefix
  is smaller).
- **Maps** support `==`/`!=` (order-independent); ordering a map is a runtime error.
- **Structs** support `==`/`!=` field-wise.
- **Functions** support `==`/`!=` by callable identity (two references to the same procedure or
  lambda compare equal); ordering throws.

## Bitwise (integers only)

`&`, `|`, `^`, `~`, `<<`, `>>` — `char` is treated as an integer here too (`float`/`double` are not).

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

`=`, plus compound assignments `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`.

```cpp
int32 accumulate(int32 n) {
    int32 total = 0;
    for (int32 i = 1; i <= n; i += 1) {
        total += i;
        total <<= 0; // <<=, >>=, |=, &=, ^=, %= are all supported
    }
    return total;
}
```

`=` on a container assigns the reference — both names then share the same elements. Slices
(`a[1:3]`) produce independent copies.

!!! note
    Compound assignment on an [external variable](../examples/05-external-variables.md) requires
    both a getter and a setter to be registered, since the interpreter needs to read-modify-write.

## Indexing, slicing, member access

```cpp
int32 last(int32[] a) { return a[-1]; }        // negative index: last element
int32[] tail(int32[] a) { return a[-2:]; }     // slice: last two
int32 lookup(map<string, int32> m, string k) { return m[k]; }
int32 getX(Point p) { return p.x; }            // struct field
```

See [Data Types](data-types.md#indexing-and-slicing) for slice semantics.

## Ternary

```cpp
int32 maxOf(int32 a, int32 b) {
    return a > b ? a : b;
}
```

Next: [Control Flow](control-flow.md).

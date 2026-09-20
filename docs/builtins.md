# Built-in Functions

Builtins are always available inside scripts (unless the host disables them — see
[Sandboxing](embedding.md#sandboxing-untrusted-scripts)). They are resolved after local
variables and procedures, so a script can shadow a builtin name with its own definition.

## Collections

Works on arrays, maps, and strings as noted.

| Function | Description |
|---|---|
| `len(x)` | Length of a string, element count of an array, or entry count of a map. |
| `push(arr, v)` | Appends `v` to `arr` in place; returns the new length. |
| `pop(arr)` | Removes and returns the last element; runtime error on empty. |
| `insert(arr, i, v)` | Inserts `v` at index `i` (shifts later elements right). |
| `removeAt(arr, i)` | Removes and returns the element at index `i`. |
| `clear(x)` | Empties an array or map in place. |
| `has(m, k)` | `true` if map `m` contains key `k`. |
| `remove(m, k)` | Removes key `k` from map `m`; returns `true` if it existed. |
| `keys(m)` | Array of the map's keys (sorted order). |
| `values(m)` | Array of the map's values (in key order). |
| `size(m)` | Entry count of a map (same as `len`). |
| `isMap(x)` | `true` if `x` is a map. |
| `isArray(x)` | `true` if `x` is an array. |
| `contains(x, v)` | `true` if string `x` contains substring `v`, or array `x` has an element equal to `v`. |
| `reverse(x)` | Reverses an array in place (returns it) or returns a reversed string. |

```cpp
int32 demo() {
    int32[] a = [1, 2, 3];
    insert(a, 1, 9);        // [1, 9, 2, 3]
    removeAt(a, 0);         // [9, 2, 3]
    return len(a);          // 3
}
```

## Strings

| Function | Description |
|---|---|
| `substr(s, start[, len])` | Substring from `start` (0-based); optional length. Errors if `start` is out of bounds. |
| `charAt(s, i)` | `char` at index `i`. |
| `indexOf(s, sub[, from])` | Index of first occurrence of `sub`, or `-1`. |
| `startsWith(s, p)` / `endsWith(s, p)` | Prefix/suffix tests. |
| `toUpper(s)` / `toLower(s)` | ASCII case conversion. |
| `trim(s)` | Strips leading/trailing whitespace. |
| `replace(s, from, to)` | Replaces all occurrences of `from` with `to`. |
| `split(s[, delim])` | Splits into a `string[]` (default delimiter `,`); empty delimiter splits into characters. |
| `join(arr[, delim])` | Concatenates elements (stringified) with `delim` between them. |
| `repeat(s, n)` | `s` repeated `n` times. |
| `format(fmt, args...)` | Substitutes `{}` placeholders left to right; `{{`/`}}` escape literal braces. |

```cpp
string demo() {
    string[] parts = split("a,b,c");       // ["a","b","c"]
    return format("{}+{}={}", parts[0], parts[1], join(parts, "+"));
}
```

## Math

| Function | Description |
|---|---|
| `abs(x)` | Absolute value (integer or float). |
| `min(a, b)` / `max(a, b)` | Numeric minimum/maximum. |
| `clamp(x, lo, hi)` | Constrains `x` to `[lo, hi]`. |
| `pow(b, e)` | `b` raised to `e` (double). |
| `sqrt(x)` | Square root. |
| `floor` / `ceil` / `round` / `trunc` | Rounding, as in `math.h`. |
| `fmod(a, b)` | Floating-point remainder (same as `a % b` on floats). |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2(y, x)` | Trigonometry (double). |
| `exp(x)` | `e^x`. |
| `log(x)` / `log10(x)` | Natural / base-10 logarithm. |
| `random()` | Random `double` in `[0, 1)`. |
| `randInt(lo, hi)` | Random integer in `[lo, hi]`. |
| `srand(seed)` | Seeds the RNG used by `random`/`randInt`. |
| `pi()` | π as a `double`. |

## Conversion

| Function | Description |
|---|---|
| `toInt(x)` | Converts any value to `int64` (truncates floats, parses strings, bool→0/1, char→code). |
| `toUInt(x)` | Converts to `uint64`. |
| `toDouble(x)` / `toFloat(x)` | Numeric conversion to `double`/`float`. |
| `toString(x)` | Stringifies any value, including arrays/maps/structs (`"[1, 2]"`, `"{k: v}"`). |
| `toBool(x)` | Converts to `bool` (nonzero, non-empty). |
| `toChar(x)` | Integer code → `char`, or first char of a string. |
| `parseInt(s)` | Parses a string to `int64`; **throws** on malformed input. |
| `parseDouble(s)` | Parses a string to `double`; **throws** on malformed input. |
| `typeof(x)` | Type name of `x`, e.g. `"int32[][]"`, `"map<string, int32>"`, `"fn(int32) -> int32"`. |

`parseInt`/`parseDouble` failures are ordinary runtime errors — catchable with
[`try`/`catch`](language/control-flow.md#try-catch-finally-throw).

## I/O and diagnostics

| Function | Description |
|---|---|
| `print(x)` | Writes the stringified value to stdout. |
| `println(x)` | Same, plus a newline. |
| `error(msg)` | Raises a runtime error with the stringified message. |
| `assert(cond[, msg])` | Runtime error (`"Assertion failed"` or `msg`) when `cond` is false; returns `true` otherwise. |

`print`/`println` write to the host process's stdout — hosts embedding CxxScript in a sandboxed
context can redirect them with `setOutputCallback` or disable them via `disableBuiltin` (see
[API Reference](api-reference.md#sandbox-controls)).

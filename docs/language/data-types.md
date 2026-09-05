# Data Types & Arrays

## Scalar types

| Type | Description |
|---|---|
| `int8`, `uint8` | 8-bit signed / unsigned integer |
| `int16`, `uint16` | 16-bit signed / unsigned integer |
| `int32`, `uint32` | 32-bit signed / unsigned integer |
| `int64`, `uint64` | 64-bit signed / unsigned integer |
| `float` | Single-precision (32-bit) floating point |
| `double` | Double-precision (64-bit) floating point |
| `char` | A single character, written with single quotes, e.g. `'a'` |
| `string` | UTF-8 text |
| `bool` | `true` / `false` |

### `float` vs `double`

`float` and `double` behave the same way except for precision/width. Mixing them promotes to
`double` (the wider type); mixing either with an integer promotes to that floating type:

```cpp
float half(int32 n) {
    return n / 2.0; // 2.0 is a double literal, but the result converts back to float
}

double combine(float a, double b) {
    return a + b; // float + double -> double
}
```

`%` (modulo) is not supported on `float`/`double`, same as before.

### `char`

`char` literals use single quotes and support the same escape sequences as strings (`\'`, `\"`,
`\\`, `\n`, `\t`, `\r`, `\0`). A `char` compares and converts like a small integer (its code point),
but concatenates as the literal character rather than its numeric code:

```cpp
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }

string gradeMessage(char grade) {
    return "Grade: " + grade; // "Grade: A", not "Grade: 65"
}

int32 nextCode(char c) { return c + 1; } // promotes to int32, e.g. 'a' -> 98
char nextChar(char c) { return c + 1; }  // stays char, e.g. 'a' -> 'b'
```

## Arrays

Any scalar type can be turned into a typed array by appending `[]`, e.g. `int32[]`, `string[]`,
`float[]`, `char[]`.

```cpp
int32 arraysDemo(int32 x) {
    int32[] nums = [1, 2, x];   // array literal
    push(nums, 10);             // nums = [1, 2, x, 10]
    int32 last = pop(nums);     // last = 10, nums = [1, 2, x]
    nums[0] = nums[0] + 5;      // in-place mutation via indexing
    return nums[0] + len(nums); // (1+5) + 3 = 9 when x == 3
}
```

### Array built-ins

| Function | Description |
|---|---|
| `len(arr)` | Number of elements in the array |
| `push(arr, value)` | Appends `value`, returns the new length |
| `pop(arr)` | Removes and returns the last element (runtime error if empty) |
| `arr[i]` | Read the element at index `i` |
| `arr[i] = value` | Write the element at index `i` |

See [Arrays example](../examples/03-arrays.md) for a complete walkthrough.

Next: [Operators](operators.md).

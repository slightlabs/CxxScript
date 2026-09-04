# Data Types & Arrays

## Scalar types

| Type | Description |
|---|---|
| `int8`, `uint8` | 8-bit signed / unsigned integer |
| `int16`, `uint16` | 16-bit signed / unsigned integer |
| `int32`, `uint32` | 32-bit signed / unsigned integer |
| `int64`, `uint64` | 64-bit signed / unsigned integer |
| `double` | Double-precision floating point |
| `string` | UTF-8 text |
| `bool` | `true` / `false` |

## Arrays

Any scalar type can be turned into a typed array by appending `[]`, e.g. `int32[]`, `string[]`.

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

# Data Types

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
| `void` | No value — procedure return types and untyped `throw`/`catch` contexts |

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

`%` on floating-point operands uses `fmod` semantics (e.g. `7.5 % 2.0` → `1.5`).

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

### `auto`

Local variables (and `for`-each loop variables) may use `auto` to deduce the type from the
initializer. The deduced type is still static — assigning an incompatible value later is a
compile error:

```cpp
int32 demo() {
    auto n = 42;              // int32
    auto s = "text";          // string
    auto grid = [[1, 2]];     // int32[][]
    for (auto x : grid[0]) { n += x; }
    return n;
}
```

`auto` requires an initializer (`auto x;` is a compile error) and cannot be combined with `[]`.

## Arrays

Any type — scalar, `map`, `struct`, or another array — can be turned into a typed array by
appending `[]`, e.g. `int32[]`, `string[]`, `Point[]`, `int32[][]`.

```cpp
int32 arraysDemo(int32 x) {
    int32[] nums = [1, 2, x];   // array literal
    push(nums, 10);             // nums = [1, 2, x, 10]
    int32 last = pop(nums);     // last = 10, nums = [1, 2, x]
    nums[0] = nums[0] + 5;      // in-place mutation via indexing
    return nums[0] + len(nums); // (1+5) + 3 = 9 when x == 3
}
```

Arrays are reference types: assigning or passing an array shares the same underlying elements
until a slice or `concat` copies.

### Indexing and slicing

Indices are zero-based. **Negative indices count from the end** (`a[-1]` is the last element),
and slices `a[begin:end]` produce a new array/string with `begin` inclusive and `end` exclusive.
Either bound may be omitted, negative bounds count from the end, and out-of-range bounds clamp:

```cpp
string demo() {
    int32[] a = [1, 2, 3, 4, 5];
    int32 last   = a[-1];     // 5
    int32[] mid  = a[1:3];    // [2, 3] — a copy
    int32[] tail = a[-2:];    // [4, 5]
    int32[] head = a[:-1];    // [1, 2, 3, 4]
    string  s    = "hello"[1:3]; // "el"
    return toString(len(tail));
}
```

Indexing a `map` reads by key (`m["k"]`); slicing applies to arrays and strings only.

### Nested containers

Arrays nest arbitrarily — arrays of arrays, arrays of maps, arrays of structs — and `map`
values can themselves be maps, arrays, or structs:

```cpp
int32 nested() {
    int32[][] grid = [[1, 2], [3, 4]];
    map<string, int32[]> table = {"evens": [2, 4], "odds": [1, 3]};
    map<string, map<string, int32>> deep = {"a": {"x": 5}};
    return grid[1][0] + table["odds"][1] + deep["a"]["x"]; // 3 + 3 + 5
}
```

### Array equality and ordering

`==`/`!=` compare element-wise (recursively for nested containers). Ordering operators
`<`, `<=`, `>`, `>=` compare **lexicographically** — `[1, 2] < [1, 3]`, and a strict prefix
is smaller (`[1] < [1, 0]`). Maps support `==`/`!=` (order-independent); ordering maps throws.
Struct values support `==`/`!=` field-wise; function values compare by identity of the
underlying callable.

Self-referencing structures are guarded: comparison and `toString` stop at a fixed depth
instead of recursing forever.

## Maps

`map<K, V>` stores key→value pairs. Keys must be scalar (numbers, `string`, `char`, `bool`);
values may be any type including containers and structs.

```cpp
int32 mapsDemo() {
    map<string, int32> ages = {"ann": 30, "bob": 25};
    ages["carl"] = 41;             // insert/overwrite
    bool hasAnn = has(ages, "ann");
    int32 ann = ages["ann"];       // read by key
    remove(ages, "bob");
    return len(ages) + ann;        // 2 + 30
}
```

Missing-key reads throw a runtime error — use `has(m, k)` to check first. `keys(m)` and
`values(m)` return arrays; `size(m)`/`len(m)` count entries.

## Structs

`struct` declares a record type with typed fields and optional methods. Construct positionally
(`Point(1, 2)`); access fields and methods with `.`; `this` refers to the receiver inside a
method, and sibling methods are callable by bare name:

```cpp
struct Point {
    int32 x;
    int32 y;
    int32 mag() { return x * x + y * y; }   // method: x, y, this
    void scale(int32 k) { x *= k; y *= k; } // implicit this writes
}

int32 structDemo() {
    Point p = Point(3, 4);
    p.scale(2);
    auto bound = p.mag;   // bound method value
    return bound();       // 100
}
```

A struct may contain fields of its own type **behind a container** (`Node[] kids`) — direct
by-value self-containment is rejected at compile time. Field/method name conflicts are compile
errors. See [Functions](functions.md#struct-methods) for method semantics.

## Enums

`enum` declares named `int64` constants. Values auto-increment (starting at 0) unless assigned
explicitly; a member may use a negative value and later members resume incrementing from it:

```cpp
enum Color { RED, GREEN, BLUE }        // 0, 1, 2
enum Code { OK = 200, NOT_FOUND = 404, ERR = -1 }

int32 enumDemo() {
    int64 c = Color.BLUE;
    return c + Code.ERR;               // 2 + (-1) = 1
}
```

Members are accessed as `Color.RED` and work in `switch` cases. Duplicate members, duplicate
enum names, and name clashes with structs/procedures are compile errors.

## `typeof` and introspection

`typeof(v)` returns the type name (`"int32[][]"`, `"map<string, int32>"`, `"Point"`,
`"fn(int32) -> int32"`); `isArray(v)`/`isMap(v)` test the runtime kind.

Next: [Operators](operators.md).

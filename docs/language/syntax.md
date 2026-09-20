# Syntax Overview

A `.script` file is a collection of top-level **declarations**: procedures, `struct`s, `enum`s,
and `import`s. Each procedure has a name, a typed parameter list, a return type, and a body of
statements — syntax intentionally mirrors C/C++ so it feels familiar.

```cpp
returnType procedureName(type1 param1, type2 param2) {
    // statements
    return value;
}
```

## Comments

```cpp
// Single-line comment

/*
 * Multi-line
 * comment
 */
```

## Imports

`import "file.script";` pulls in another script's procedures, structs, and enums. Paths are
resolved relative to the importing file; circular imports are handled (each file compiles once).
The host can restrict where imports may point — see
[Sandboxing](../embedding.md#sandboxing-untrusted-scripts).

```cpp
import "lib.script";          // lib.script's procedures become callable
import "shared/util.script";  // subdirectories are fine
```

## Variables and constants

```cpp
int32 count = 0;
const int32 MAX = 100;        // immutable: reassigning is a compile error
string name = "CxxScript";
bool ready = true;
auto inferred = 1 + 2;        // int32 — see Data Types -> auto
```

Variables must be declared before use, and every procedure parameter and return value is
statically typed — mismatches are reported as compile errors. `const` forbids rebinding the
variable (`x = ...`, `x += ...`, `x++`) and, for containers, element writes (`a[i] = ...`,
`m["k"] = ...`, `s.field = ...`). Mutating builtins like `push`/`pop` bypass the check, so
treat `const` containers as read-only by convention when passing them onward.

## Numeric literals

Integers support base prefixes and `_` digit separators; floats use `.` and `e`/`E` exponents:

```cpp
int32 hex    = 0xFF;        // 255
int32 bin    = 0b1010;      // 10
int32 oct    = 0o17;        // 15
int64 big    = 1_000_000;   // separators for readability
double sci   = 1.5e-3;
```

## String literals and escape sequences

| Escape | Meaning |
|---|---|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\0` | Null character |

Unknown escape sequences (e.g. `\x`) are preserved as-is.

```cpp
string examples() {
    string quote = "He said \"Hello\"";
    string path = "C:\\Users\\Name";
    string multiline = "Line 1\nLine 2";
    return quote;
}
```

## Character literals

`char` literals use single quotes and support the same escape sequences as strings (`\'`, `\"`,
`\\`, `\n`, `\t`, `\r`, `\0`):

```cpp
char newline() { return '\n'; }
char letterA() { return 'A'; }
```

## String concatenation

The `+` operator concatenates a `string` with any scalar value, converting it automatically —
a `char` concatenates as the literal character, not its numeric code:

```cpp
string greetUser(string name, int32 age) {
    return "Hello, " + name + "! You are " + age + " years old.";
}
```

## String interpolation

Any expression can be embedded in a string literal with `${...}`; the result is stringified.
Escape `\${` for a literal `${`:

```cpp
string demo() {
    string name = "world";
    int32 a = 1;
    int32 b = 2;
    return "hi ${name}: ${a + b}";   // "hi world: 3"
}
```

## Calling other procedures

Any procedure can call another procedure defined in the same file, or in any other file loaded
into the same `ScriptManager` (see [Multi-File Scripts](../examples/06-multi-file.md)):

```cpp
int32 square(int32 n) { return n * n; }

int32 sumOfSquares(int32 a, int32 b) {
    return square(a) + square(b);
}
```

Next: [Data Types & Arrays](data-types.md).

# Syntax Overview

A `.script` file is a collection of **procedures** (functions). Each procedure has a name, a typed
parameter list, a return type, and a body of statements — syntax intentionally mirrors C/C++ so it
feels familiar.

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

## Variables

```cpp
int32 count = 0;
string name = "CxxScript";
bool ready = true;
```

Variables must be declared with an explicit type before use, and every procedure parameter and
return value is statically typed — mismatches are reported as compile errors.

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

# Functions

CxxScript has three flavors of callable: **procedures** (top-level named functions), **lambdas**
(anonymous function values), and **struct methods**. All are statically typed.

## Procedures

```cpp
returnType name(type1 param1, type2 param2) {
    // body
    return value;    // omit (or bare `return`) for void
}
```

Procedures may be declared in any order — calls are resolved after all files are loaded, so
mutual recursion works:

```cpp
bool isEven(int32 n) { return n == 0 ? true : isOdd(n - 1); }
bool isOdd(int32 n)  { return n == 0 ? false : isEven(n - 1); }
```

## Default parameters

Trailing parameters may declare defaults; a default expression is evaluated in the callee's
scope, so it can reference earlier parameters:

```cpp
int32 f2(int32 a, int32 b = a * 2) { return a + b; }
int32 f() { return f2(5) + f2(5, 1); }  // 15 + 6
```

Defaults must be trailing — `void g(int32 a = 1, int32 b)` is a compile error — and are
type-checked against the parameter type.

## Overloading

A name may be declared more than once with different parameter signatures (by arity, by type,
or both). Calls resolve at compile time preferring exact type matches over conversions:

```cpp
int32 g(int32 a)         { return 1; }
int32 g(string a)        { return 2; }
int32 g(int32 a, int32 b){ return 3; }

int32 f() { return g("x") * 10 + g(5); }  // 2*10 + 1
```

- A call with no viable overload is a **compile error**.
- An ambiguous call (several overloads convert equally well) is a **compile warning** and a
  runtime error if executed — add an exact-match overload to silence it.
- Two declarations with the same parameter list are a compile error, regardless of return type.

## Lambdas

`fn` creates an anonymous function. Parameter types and the `-> T` return annotation are both
optional — untyped parameters accept whatever the call site converts to, and an unannotated
return type is inferred:

```cpp
auto sq  = fn(int32 x) -> int32 { return x * x; };
auto add = fn(a, b) { return a + b; };          // untyped params, inferred return
int32 n  = sq(6) + add(2, 3);                   // 36 + 5
```

Lambdas **capture** surrounding local variables by copy — a snapshot at creation:

```cpp
fn(int32) -> int32 makeAdder(int32 n) {
    return fn(int32 x) -> int32 { return x + n; };  // captures n
}
int32 f() {
    auto add5 = makeAdder(5);
    return add5(10);   // 15
}
```

## Function values and the `fn` type

Functions are first-class `Value`s with type `fn(params) -> ret`. They can be stored in
variables and arrays, passed as arguments, and returned:

```cpp
int32 applyTwice(fn(int32) -> int32 op, int32 x) { return op(op(x)); }

int32 f() {
    auto ops = [fn(x) { return x + 1; }, fn(x) { return x * 10; }];
    return applyTwice(ops[0], 1) + ops[1](2);   // (1+1+1) + 20
}
```

A bare procedure name is itself a function value — `auto r = applyTwice;` then `r(f, x)` —
and an **overloaded** name resolves at call time against whichever overload matches. Assigning
a function to a declared `fn(...)` type checks signature compatibility (parameter count and
convertibility); a mismatch is a compile error.

Two function values compare equal (`==`) when they refer to the same callable: same procedure,
same lambda body with equal captures, or same bound method receiver.

## Struct methods

A `struct` may declare methods; they run with `this` bound to the receiver. Field names and
sibling method names resolve implicitly — `x` means `this.x` and `helper()` means
`this.helper()` — unless shadowed by a local:

```cpp
struct Vec2 {
    int32 x;
    int32 y;
    int32 mag2() { return x * x + y * y; }      // implicit this
    void scale(int32 k) { x *= k; y *= k; }     // implicit writes
    int32 norm2() { return mag2() * 1; }        // sibling call
}

int32 demo() {
    Vec2 v = Vec2(3, 4);
    v.scale(2);
    auto m = v.mag2;    // bound-method value: receiver captured
    return m();         // 100
}
```

`v.mag2` produces a function value bound to that `v` — calling it later still reads and writes
the same struct instance. Structs are reference types, so `v.scale(2)` mutates the original.

## Recursion and limits

Direct and mutual recursion are supported. When the host sets execution limits
(`ScriptManager::setExecutionLimits`), recursion depth and total steps are capped — exceeding
them raises a **fatal** error that `try`/`catch` cannot suppress.

Next: [Builtins](../builtins.md).

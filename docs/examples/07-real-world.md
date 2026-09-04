# Example 7: Real-World App — E-Commerce Business Rules

A complete example that mirrors a real application: business rules for an e-commerce platform are
kept in `.script` files so they can change without recompiling the host application, while the C++
layer handles orchestration, persistence, and infrastructure.

## Architecture

```text
scripts/test_files/
├── validation_rules.script  # age / username / email / password validation
├── business_logic.script    # user level, discounts, age-restricted purchases
├── reporting.script         # formatting user profiles & purchase confirmations
└── workflows.script         # registration & purchase workflows tying it together
```

The C++ host loads all four modules into one `ScriptManager`, registers external functions
(`strlen`, `contains`, logging, database access), and drives the workflow by calling procedures.

## Business rules, in script

User level based on age and tenure:

```cpp
int32 calculateUserLevel(int32 age, int32 yearsActive) {
    if (age >= 60 || yearsActive >= 10) {
        return 3; // Premium
    }
    if (age >= 30 || yearsActive >= 5) {
        return 2; // Standard
    }
    return 1; // Basic
}
```

Discount calculation that combines tiered levels with purchase-size bonuses:

```cpp
int32 calculateDiscount(int32 userLevel, int32 purchaseAmount) {
    int32 baseDiscount = 0;
    if (userLevel == 3) {
        baseDiscount = 20;
    } else if (userLevel == 2) {
        baseDiscount = 10;
    } else {
        baseDiscount = 5;
    }

    if (purchaseAmount >= 1000) {
        baseDiscount += 5;
    }
    return baseDiscount;
}
```

Country-specific, age-restricted purchases:

```cpp
bool canPurchaseAlcohol(int32 age, string country) {
    if (country == "USA") { return age >= 21; }
    if (country == "Japan") { return age >= 20; }
    return age >= 18; // Default
}
```

## Driving it from C++

```cpp
ScriptManager manager;
std::vector<CompilationError> errors;

manager.loadScriptFile("scripts/test_files/validation_rules.script", errors);
manager.loadScriptFile("scripts/test_files/business_logic.script", errors);
manager.loadScriptFile("scripts/test_files/reporting.script", errors);
manager.loadScriptFile("scripts/test_files/workflows.script", errors);

// Native helpers the scripts rely on
manager.registerExternalFunction("logEvent", [](const std::vector<Value> &args) {
    std::cout << "[log] " << ValueHelper::toString(args[0]) << std::endl;
    return static_cast<int32_t>(0);
});

Value result;
std::string errorMessage;
manager.executeProcedure(
    "registerUser",
    {std::string("alice_smith"), static_cast<int32_t>(28)},
    result, errorMessage);
```

## Sample output

```text
Step 1: User Registration
  Welcome, alice_smith! Your registration was successful.

Step 2: First Purchase (New User)
  Purchase confirmed for alice_smith. Total: $180
  Original: $200, Discount: 10%, Final: $180

Step 4: Large Purchase After 10 Years (Premium Level)
  Purchase confirmed for alice_smith. Total: $2250
  Original: $3000, Discount: 25%, Final: $2250
```

## Why this matters

- **Separation of concerns** — business rules live in scripts (easy to change), performance-critical
  code stays in C++.
- **Non-programmer friendly** — analysts can edit `.script` files without touching the C++ codebase.
- **Testable** — the full scenario is covered by [`tests/test_real_world_app.cpp`](https://github.com/slightlabs/CxxScript/blob/main/tests/test_real_world_app.cpp).

This is the same scenario exercised end-to-end by the project's test suite — run it yourself with:

```bash
ctest -R RealWorldApp --output-on-failure
```

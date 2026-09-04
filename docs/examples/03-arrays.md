# Example 3: Arrays

Typed arrays support literals, indexing, mutation, and the `len` / `push` / `pop` built-ins.

```cpp
int32 arraysDemo(int32 x) {
    int32[] nums = [1, 2, x];
    push(nums, 10);          // nums = [1, 2, x, 10]
    int32 last = pop(nums);  // last = 10, nums = [1, 2, x]
    nums[0] = nums[0] + 5;   // mutate in-place
    return nums[0] + len(nums); // (1+5) + 3 = 9 when x == 3
}

double average(double[] values) {
    double sum = 0.0;
    for (int32 i = 0; i < len(values); i += 1) {
        sum += values[i];
    }
    return sum / len(values);
}

string joinWithCommas(string[] words) {
    string result = "";
    for (int32 i = 0; i < len(words); i += 1) {
        if (i > 0) {
            result += ", ";
        }
        result += words[i];
    }
    return result;
}
```

## Running it

```cpp
Value result;
std::string errorMessage;

manager.executeProcedure("arraysDemo", {static_cast<int32_t>(3)}, result, errorMessage);
// std::get<int32_t>(result) == 9
```

Next: [Example 4: External Functions](04-external-functions.md).

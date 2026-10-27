# OblivoneSort 1.0.0 — Usage

## 1. Requirements

- C++17 or newer.
- Header-only: no compiled library is required.
- Single-thread API: C++ standard library only.
- Parallel API: standard thread facilities plus the platform Threads library.

## 2. Header Files

```cpp
#include <oblivone/sort.hpp>      // single-thread
#include <oblivone/parallel.hpp>  // optional parallel
```

Including only `sort.hpp` does not pull in `<thread>` or require a thread library.

## 3. Quick Start

```cpp
#include <oblivone/sort.hpp>

#include <vector>

int main() {
    std::vector<double> values = {3.0, 1.0, 2.0, -0.0, 0.0};
    oblivone::sort(values);
}
```

The result is sorted in ascending order.

The sort is unstable: equal keys may change relative order.

## 4. Default Parameters

```cpp
oblivone::options opt;
opt.small_top      = 48;
opt.small_sort     = 16;
opt.leaf_sort      = 22;
opt.scan_threshold = 256;   // 0 disables nested scanning
opt.max_depth      = 16;
opt.bucket_limit   = 65536;
```

| Option | Default | Meaning |
|---|---:|---|
| `small_top` | 48 | Top-level `n <= 48` goes to fallback |
| `small_sort` | 16 | Core group `m < 16` goes to fallback |
| `leaf_sort` | 22 | Group `m < 22` uses insertion sort |
| `scan_threshold` | 256 | Nested blocks larger than this get an ordered scan; 0 disables |
| `max_depth` | 16 | Recursion limit; reaches fallback |
| `bucket_limit` | 65536 | Hard bucket-count limit per level |

`bucket_limit=0` is treated as 1.

## 5. Supported Types

Default `sort` supports arithmetic types:

- signed integers;
- unsigned integers;
- `float`;
- `double`.

For other types, use `sort_by` with a custom key.

## 6. Custom Key

```cpp
#include <oblivone/sort.hpp>

struct Item {
    int score;
    double payload;
};

std::vector<Item> items = ...;

oblivone::sort_by(items, [](const Item& item) {
    return oblivone::key_of(item.score);
});
```

The key must be order-preserving and convertible to `uint64_t`.

For a non-arithmetic key:

```cpp
oblivone::sort_by(items, [](const Item& item) -> std::uint64_t {
    return static_cast<std::uint64_t>(item.rank);
});
```

`sort_by` does not use the native double leaf comparison; it always compares through the key.

## 7. Custom Fallback

```cpp
struct MyFallback {
    template <class T, class Less>
    void operator()(T* first, T* last, Less less) const {
        std::sort(first, last, less);
    }
};

std::vector<double> values = ...;
oblivone::sort(values, oblivone::options{}, MyFallback{});
```

Requirements:

- use the supplied `less`;
- O(k log k) worst case;
- preserve element count;
- may throw.

The default fallback uses insertion sort for arithmetic segments of at most 96 elements and `std::sort` otherwise.

## 8. Floating-Point Semantics

- All NaNs sort after all non-NaN values.
- `-0.0` and `+0.0` are considered equal.
- `+Inf` and `-Inf` are supported.
- NaN-to-NaN order is unspecified.

## 9. Parallel Sort

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 12;       // 0 = automatic
popt.min_n = 2000000;
popt.max_buckets = 65536;
popt.scratch_budget = 0; // 0 = n * sizeof(T)

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

`parallel_sort` performs:

- serial prechecks;
- parallel root count;
- parallel root scatter;
- parallel copy;
- parallel depth-1 groups.

Group recursion remains serial.

### 9.1 Defaults

| Option | Default | Meaning |
|---|---:|---|
| `threads` | 0 | Auto: 4 below 1e7, 12 at 1e7 and above, capped by hardware |
| `min_n` | 2,000,000 | Smaller inputs use serial dispatcher |
| `max_buckets` | 65,536 | Per-thread count table limit |
| `scratch_budget` | 0 | 0 means `n * sizeof(T)` |
| `pool` | nullptr | Null creates a temporary pool per call |

### 9.2 Injecting a Thread Pool

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
popt.min_n = 2000000;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

The pool must outlive the sort call.

A single pool serializes its `run()` calls. Different pools may run concurrently.

### 9.3 Custom Key

```cpp
oblivone::parallel_sort_by(values, key_function,
                            oblivone::options{}, popt);
```

The custom key must be thread-safe because it is called from multiple workers.

## 10. Semantics and Limits

- Unstable sort.
- `n > UINT32_MAX` goes to fallback.
- Comparator, key, and fallback may throw; only the basic exception guarantee is provided.
- Default `sort` never creates threads.
- Parallel APIs require explicit calls.

## 11. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

### 11.1 CMake Targets

```cmake
target_link_libraries(app PRIVATE oblivone::oblivone)  # single-thread
target_link_libraries(app PRIVATE oblivone::parallel)  # parallel, links Threads
```

### 11.2 Install

```bash
cmake --install build --prefix /your/prefix
```

Consumer:

```cmake
find_package(oblivone CONFIG REQUIRED)
target_link_libraries(app PRIVATE oblivone::oblivone)
# or
target_link_libraries(app PRIVATE oblivone::parallel)
```

## 12. When to Use

Good fit:

- random or smooth numeric distributions;
- large `n`;
- header-only, no third-party dependency;
- O(n) extra memory is acceptable;
- unstable sort is acceptable.

Poor fit:

- already sorted or nearly sorted data;
- many long runs, organ-pipe, push-front/push-back patterns;
- stable sorting is required;
- non-numeric types without a custom key;
- strict memory limits;
- tiny arrays where `std::sort` is simpler.

## 13. Minimal Complete Example

```cpp
#include <oblivone/sort.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

int main() {
    std::vector<std::int32_t> values = {5, -1, 3, 3, 0};

    oblivone::options opt;
    opt.bucket_limit = 1024;

    oblivone::sort(values, opt);

    return std::is_sorted(values.begin(), values.end()) ? 0 : 1;
}
```

## 14. Parallel Example

```cpp
#include <oblivone/parallel.hpp>

#include <vector>

int main() {
    std::vector<double> values = ...;

    oblivone::thread_pool pool(8);

    oblivone::parallel_options popt;
    popt.pool = &pool;
    popt.min_n = 2000000;

    oblivone::parallel_sort(values, oblivone::options{}, popt);
}
```

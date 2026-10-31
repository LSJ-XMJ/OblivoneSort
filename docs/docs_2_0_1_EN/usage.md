# OblivoneSort 2.0.1 Usage

> **Note:** This document is a translation and may contain errors;
> the Chinese documents under `docs/docs_2_0_1/` are authoritative.

## 1. Headers

```cpp
#include <oblivone/sort.hpp>          // serial
#include <oblivone/parallel.hpp>      // installed parallel API
#include <oblivone/advanced/sort.hpp> // experimental, not installed
```

---

## 2. Serial

```cpp
std::vector<double> values = ...;
oblivone::sort(values);
```

Custom key:

```cpp
struct Item {
    int key;
};

std::vector<Item> items = ...;

oblivone::sort_by(items, [](const Item& item) {
    return oblivone::key_of(item.key);
});
```

The key must be order-preserving.

---

## 3. Parallel

```cpp
#include <oblivone/parallel.hpp>

std::vector<double> values = ...;

oblivone::parallel_options popt;
popt.threads = 12;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

Custom key:

```cpp
oblivone::parallel_sort_by(
    items,
    [](const Item& item) {
        return oblivone::key_of(item.key);
    },
    oblivone::options{},
    popt);
```

---

## 4. options

```cpp
struct oblivone::options {
    std::size_t small_top      = 48;
    std::size_t small_sort     = 16;
    std::size_t leaf_sort      = 22;
    std::size_t scan_threshold = 256;
    std::size_t max_depth      = 16;
    std::size_t bucket_limit   = 65536;
    bool cache_bucket_index    = false;
};
```

---

## 5. parallel_options

```cpp
struct oblivone::parallel_options {
    unsigned threads = 0;                // 0 = automatic
    std::size_t min_n = 2000000;
    std::size_t max_buckets = 65536;
    std::size_t coarse_buckets = 0;      // 0 = automatic
    std::size_t local_bucket_limit = 0;  // 0 = automatic
    std::size_t scratch_budget = 0;      // 0 = n * sizeof(T)
    oblivone::thread_pool* pool = nullptr;
};
```

Automatic values:

```text
P = clamp(n / 8000, 512, 1024)

B:
  n <=  3,000,000  -> 1024
  n <= 15,000,000  -> 2048
  n >  15,000,000  -> 4096
```

---

## 6. Reusable thread pool

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
popt.min_n = 1000;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

A pool must be used by one caller thread at a time.

---

## 7. Advanced experimental backend

```cpp
#include <oblivone/advanced/sort.hpp>

oblivone::advanced::options popt;
popt.threads = 12;

oblivone::advanced::parallel_sort(values, oblivone::options{}, popt);
```

Reusable executor:

```cpp
oblivone::advanced::executor ex(12);

oblivone::advanced::options popt;
popt.min_n = 1000;

oblivone::advanced::parallel_sort(ex, values, oblivone::options{}, popt);
```

The advanced backend is not installed and has no stable API guarantee.

---

## 8. Requirements

```text
unstable sort
n extra elements required
NaN sorts last
-0 == +0
custom key must be order-preserving
```

---

## 9. CMake

```cmake
find_package(oblivone CONFIG REQUIRED)

target_link_libraries(app PRIVATE oblivone::oblivone)  # serial
target_link_libraries(app PRIVATE oblivone::parallel)  # parallel
```

`oblivone::parallel` links `Threads::Threads` and `oblivone::oblivone`.

The installed package does not include `include/oblivone/advanced/`.

---

## 10. Version

```cpp
#include <oblivone/version.hpp>

oblivone::version // "2.0.1"
```

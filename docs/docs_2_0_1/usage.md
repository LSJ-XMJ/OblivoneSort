# 2.0.1 使用说明

## 1. 头文件

```cpp
#include <oblivone/sort.hpp>          // 串行
#include <oblivone/parallel.hpp>      // 正式并行
#include <oblivone/advanced/sort.hpp> // 实验后端，不安装
```

---

## 2. 串行

```cpp
std::vector<double> values = ...;
oblivone::sort(values);
```

自定义 key：

```cpp
struct Item {
    int key;
};

std::vector<Item> items = ...;

oblivone::sort_by(items, [](const Item& item) {
    return oblivone::key_of(item.key);
});
```

要求 key 保序。

---

## 3. 并行

```cpp
#include <oblivone/parallel.hpp>

std::vector<double> values = ...;

oblivone::parallel_options popt;
popt.threads = 12;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

自定义 key：

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
    int max_depth             = 16;
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

自动值：

```text
P = clamp(n / 8000, 512, 1024)

B:
  n <=  3,000,000  -> 1024
  n <= 15,000,000  -> 2048
  n >  15,000,000  -> 4096
```

---

## 6. 持久化线程池

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
popt.min_n = 1000;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

一个 pool 只能由一个调用线程使用。

---

## 7. advanced 实验后端

```cpp
#include <oblivone/advanced/sort.hpp>

oblivone::advanced::options popt;
popt.threads = 12;

oblivone::advanced::parallel_sort(values, oblivone::options{}, popt);
```

executor：

```cpp
oblivone::advanced::executor ex(12);

oblivone::advanced::options popt;
popt.min_n = 1000;

oblivone::advanced::parallel_sort(ex, values, oblivone::options{}, popt);
```

advanced 不安装，不承诺稳定 API。

---

## 8. 数据要求

```text
不稳定排序
需要额外 n 个元素空间
NaN 最后
-0 == +0
custom key 必须保序
```

---

## 9. CMake

```cmake
find_package(oblivone CONFIG REQUIRED)

target_link_libraries(app PRIVATE oblivone::oblivone)  # serial
target_link_libraries(app PRIVATE oblivone::parallel)  # parallel
```

`oblivone::parallel` 会链接：

```text
Threads::Threads
oblivone::oblivone
```

安装包不包含 `include/oblivone/advanced/`。

---

## 10. 版本

```cpp
#include <oblivone/version.hpp>

oblivone::version // "2.0.1"
```

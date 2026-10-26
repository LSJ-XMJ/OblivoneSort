# OblivoneSort

C++17、header-only、非稳定的分布排序库。

OblivoneSort 把值映射成保序 `uint64_t` key，按 key 直接计算桶号，计数、前缀和、散播，再对桶内元素递归或插入排序。

> 适合随机/平滑数值分布；不追求结构化数据，TimSort 在该场景更强。

## 快速开始

```cpp
#include <oblivone/sort.hpp>

std::vector<double> values = ...;
oblivone::sort(values);
```

自定义 key：

```cpp
oblivone::sort_by(items, [](const Item& item) {
    return static_cast<std::uint64_t>(item.score);
});
```

## 并行

并行是独立头文件，核心头不依赖 `<thread>`：

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 12;      // 0 = 自动
popt.min_n = 2000000;
popt.scratch_budget = 0;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

也可以注入线程池：

```cpp
oblivone::thread_pool pool(12);
oblivone::parallel_options popt;
popt.pool = &pool;
oblivone::parallel_sort(values, oblivone::options{}, popt);
```

## 参数

```cpp
oblivone::options opt;
opt.small_top      = 48;
opt.small_sort     = 16;
opt.leaf_sort      = 22;
opt.scan_threshold = 256;   // 0 = 关闭
opt.max_depth      = 16;
opt.bucket_limit   = 65536;
```

## 语义

- 不稳定排序；
- double 的 NaN 全部最后，`-0 == +0`；
- `n > UINT32_MAX` 直接走 fallback；
- 默认 fallback：算术类型 n≤96 用插入，其余 `std::sort`；
- 用户 fallback 必须使用同一个 less，最坏 O(k log k)；
- 异常提供基本保证。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake 目标：

- 单线程：`oblivone::oblivone`；
- 并行：`oblivone::parallel`（自动链接 Threads）。

```cmake
target_link_libraries(app PRIVATE oblivone::oblivone)
# 或
target_link_libraries(app PRIVATE oblivone::parallel)
```

## 文档

设计要求、参数、算法、并行和测试见 [doc/README.md](doc/README.md)。

## 许可

Apache-2.0，无专利。

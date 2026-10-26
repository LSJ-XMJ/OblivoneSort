# OblivoneSort

[![CI](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml/badge.svg)](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

C++17、header-only、非稳定的分布排序库。

OblivoneSort 把值映射成保序 `uint64_t` key，按 key 直接计算桶号，计数、前缀和、散播，再对桶内元素递归或插入排序。

> **定位**：随机/平滑数值分布。
>
> 不追求结构化数据，TimSort 在 organ/runs/push 等场景更强。

当前版本：`v1.0.0`。

## 特性

- 单线程 `sort` / `sort_by`；
- 可选并行 `parallel_sort` / `parallel_sort_by`；
- double 的 NaN 最后，`-0 == +0`；
- 默认 fallback 对小段使用插入排序；
- 可注入自定义 key / fallback / 线程池；
- 核心头不依赖 `<thread>`；
- 不使用任何第三方运行时库；
- Apache-2.0，无专利。

## 快速开始

```cpp
#include <oblivone/sort.hpp>

std::vector<double> values = ...;
oblivone::sort(values);
```

自定义 key：

```cpp
struct Item {
    int score;
};

std::vector<Item> items = ...;
oblivone::sort_by(items, [](const Item& item) {
    return oblivone::key_of(item.score);
});
```

自定义 key 必须保序并返回可转换为 `uint64_t` 的值。

## 并行

并行是独立头文件：

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 12;       // 0 = 自动
popt.min_n = 2000000;
popt.max_buckets = 65536;
popt.scratch_budget = 0; // 0 = n * sizeof(T)

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

注入常驻线程池：

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
oblivone::parallel_sort(values, oblivone::options{}, popt);
```

默认 `sort` 永远不会创建线程；并行必须显式调用。

## 参数

```cpp
oblivone::options opt;
opt.small_top      = 48;     // 顶层 n <= 48 -> fallback
opt.small_sort     = 16;     // 核心组 m < 16 -> fallback
opt.leaf_sort      = 22;     // 组 m < 22 -> 插入
opt.scan_threshold = 256;    // 嵌套扫描；0 = 关闭
opt.max_depth      = 16;     // 递归上限
opt.bucket_limit   = 65536;  // 每层桶数上限
```

并行：

```cpp
oblivone::parallel_options popt;
// threads        = 0
// min_n          = 2000000
// max_buckets    = 65536
// scratch_budget = 0
// pool           = nullptr
```

## 语义

- 不稳定排序；
- 所有 NaN 排在非 NaN 之后；
- `-0.0` 与 `+0.0` 视为相等；
- `n > UINT32_MAX` 直接走 fallback；
- 默认 fallback：算术类型 n≤96 插入，其余 `std::sort`；
- 用户 fallback 必须使用同一个 less，最坏 O(k log k)；
- 异常只提供基本保证；
- 并行会并发调用用户 key/fallback，二者必须可重入且线程安全。

## 性能

以下为本机 GCC 15.2、`-O2`、单次中位数，仅用于展示量级；绝对值受 CPU/热漂移影响。

### 单线程 1e6

| 模式 | std::sort | OblivoneSort |
|---|---:|---:|
| random | 81.6 ms | **23.0 ms** |
| sorted | 31.3 ms | **1.64 ms** |
| organ | 97.7 ms | **18.3 ms** |

### 并行 random

| n | 串行 | 并行 | 加速 |
|---:|---:|---:|---:|
| 1e6 | 25.0 ms | 13.9 ms | 1.80× |
| 1e7 | 258.3 ms | 87.7 ms | 2.94× |
| 2e7 | 524.2 ms | 176.2 ms | 2.98× |

并行默认 `min_n=2e6`；1e6 需要显式调低。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

安装：

```bash
cmake --install build --prefix /your/prefix
```

CMake 目标：

```cmake
target_link_libraries(app PRIVATE oblivone::oblivone)  # 单线程
target_link_libraries(app PRIVATE oblivone::parallel)  # 并行，自动链接 Threads
```

## 适用与不适用

适合：

- 随机浮点/整数；
- 分布相对平滑的数据；
- 大 n 且允许并行；
- 需要 header-only、无第三方依赖的场景。

不适合：

- 已排序/近有序为主；
- 大量 run/结构化数据；
- 要求稳定排序；
- 不能分配额外 `n` 缓冲；
- n 很小时（直接 `std::sort` 更简单）。

## 文档

公开仓库只保留本 README；完整设计记录不随仓库发布。

## 作者

肖明杰（Xiao Mingjie，笔名）。

公开仓库不发布私人邮箱；联系请使用 GitHub 账号 [@LSJ-XMJ](https://github.com/LSJ-XMJ)。

## 许可

Apache-2.0。见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

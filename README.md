# OblivoneSort

[![CI](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml/badge.svg)](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.23675733.svg)](https://doi.org/10.5281/zenodo.23675733)

> **2.0.0**
>
> 正式并行 API 为 `oblivone::parallel`；`oblivone::advanced` 下的调度器为实验性实现，不进入安装包。
>
> 中文文档：[总览](docs/docs_2_0_0/README.md)
>
> English documentation:
> [Design](docs/docs_2_0_0_EN/design.md) ·
> [Parallel design](docs/docs_2_0_0_EN/parallel-design.md) ·
> [Usage](docs/docs_2_0_0_EN/usage.md) ·
> [Benchmarks](docs/docs_2_0_0_EN/benchmarks.md)

C++17、header-only、非稳定分布排序。

OblivoneSort 将元素映射为保序的 `uint64_t` key，直接由 key 计算桶号，然后执行
count、prefix sum、scatter，并在桶内递归或使用插入排序。

> **适用范围**：random / 平滑数值分布。
>
> 不针对结构化数据；在 organ / runs / push 等模式上 TimSort 更强。

当前版本：`2.0.0`。

## 特性

- 单线程 `sort` / `sort_by`；
- 正式并行 `parallel_sort` / `parallel_sort_by`；
- 可复用 `thread_pool`；
- advanced work-stealing 调度器，实验性，不安装；
- 可选桶号缓存，默认关闭；
- double/float 的 NaN 排在最后，`-0 == +0`；
- 小范围默认使用插入排序；
- 支持自定义 key 和自定义 fallback；
- 串行核心不依赖 `<thread>`；
- 无第三方运行时依赖；
- Apache-2.0，无专利限制。

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

自定义 key 必须保序，并且可转换为 `uint64_t`。

## 并行

2.0 的并行层是独立头文件：

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 12;
popt.min_n = 2000000;
popt.max_buckets = 65536;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

复用线程池：

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
oblivone::parallel_sort(values, oblivone::options{}, popt);
```

`sort` 不会创建线程。通过并行 API 使用的自定义 key 或 fallback 必须可重入且线程安全。

## 选项

```cpp
oblivone::options opt;
opt.small_top      = 48;     // 顶层 n <= 48 时交给 fallback
opt.small_sort     = 16;     // 核心组 m < 16 时交给 fallback
opt.leaf_sort      = 22;     // 组 m < 22 时使用插入排序
opt.scan_threshold = 256;    // 嵌套块有序扫描；0 表示关闭
opt.max_depth      = 16;     // 递归深度上限
opt.bucket_limit   = 65536;  // 每层桶数上限
opt.cache_bucket_index = false;  // 桶号缓存
```

### 桶号缓存

```cpp
oblivone::options opt;
opt.cache_bucket_index = true;
oblivone::sort_by(values, expensive_key, opt);
```

规则：

- 默认关闭；
- 仅当桶数 `B <= 65536` 时生效；
- 使用 `uint16_t` 表；
- `B > 65536` 时自动禁用；
- 额外占用约 `2n` 字节；
- 适合昂贵自定义 key；默认整数 key 不一定受益。

## 语义

- 不稳定排序；
- 所有 NaN 排在非 NaN 之后；
- `-0.0` 与 `+0.0` 比较相等；
- `n > UINT32_MAX` 直接使用 fallback；
- 默认 fallback：算术范围且 `n <= 96` 使用插入排序，否则使用 `std::sort`；
- 自定义 fallback 必须使用相同的 `less`；最坏复杂度为 O(k log k)；
- 异常只提供基本保证。

## 性能

2.0.0 对数值输入使用值域粗桶。

标准对比：13 种模式、double、GCC 15.2、Boost 1.89；1e7 数据使用
12 线程。完整数据见 [benchmarks.md](docs/docs_2_0_0/benchmarks.md)。

1e7 部分结果：

| 模式 | simple | advanced | 最快 Boost |
|---|---:|---:|---:|
| random | **54.2 ms** | 58.5 ms | 122.6 ms |
| runs | **52.6 ms** | 55.5 ms | 98.9 ms |
| sawtooth | **40.3 ms** | 43.7 ms | 46.8 ms |
| organ | 63.3 ms | 66.3 ms | **29.5 ms** |
| push back | 64.9 ms | 64.6 ms | **32.5 ms** |

并行在 random / 平滑数据上最强。TimSort 和 Boost `sample_sort` 在
organ、push 类输入上更强。

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

CMake target：

```cmake
target_link_libraries(app PRIVATE oblivone::oblivone)   # 串行
target_link_libraries(app PRIVATE oblivone::parallel)   # 并行，链接 Threads
```

串行 target 不链接 Threads。

## 适用与不适用

适合：

- random 浮点或整数数据；
- 分布相对平滑；
- 大 n，且要求 header-only、无第三方运行时依赖；
- 串行核心不能创建线程的环境。

不适用：

- 基本有序或接近有序数据；
- 大量 runs / 结构化数据；
- 要求稳定排序；
- 没有额外 `n` 缓冲区；
- 极小 n，直接使用 `std::sort` 更简单。

## 发布状态

```text
版本头文件       2.0.0
CMake 项目       2.0.0
发布 tag         v2.0.0
发布类型         正式
目标分支         main
发布来源         v2.0.0-dev
main             已合并
额外文档         docs/docs_2_0_0, docs/docs_2_0_0_EN
Zenodo DOI       10.5281/zenodo.23675733
CITATION DOI     10.5281/zenodo.23675733
```

## 作者

肖明杰。

## 许可证

Apache-2.0。见 [LICENSE](LICENSE) 和 [NOTICE](NOTICE)。

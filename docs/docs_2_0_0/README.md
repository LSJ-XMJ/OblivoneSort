# OblivoneSort 2.0.0

C++17 header-only distribution sort.

```text
serial sort / sort_by
simple parallel_sort / parallel_sort_by
advanced experimental scheduler
```

## 特性

- 保序 `uint64_t` key；
- 直接桶映射；
- count / prefix / scatter / group sort；
- double/float NaN 最后，`-0 == +0`；
- 可注入自定义 key 与 fallback；
- 值域两级粗桶并行；
- 可选桶号缓存；
- 无第三方运行时依赖；
- Apache-2.0。

## 快速使用

```cpp
#include <oblivone/parallel.hpp>

std::vector<double> values = ...;

oblivone::parallel_options popt;
popt.threads = 12;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

串行：

```cpp
#include <oblivone/sort.hpp>

oblivone::sort(values);
```

## 文档

- [单线程设计](design.md)
- [并行设计](parallel-design.md)
- [使用说明](usage.md)
- [强势算法对比](benchmarks.md)
- [测试记录](test-record.md)

英文：

- [Design](../docs_2_0_0_EN/design.md)
- [Parallel design](../docs_2_0_0_EN/parallel-design.md)
- [Usage](../docs_2_0_0_EN/usage.md)
- [Benchmarks](../docs_2_0_0_EN/benchmarks.md)

原始测试数据：

- [GCC](test-data/gcc.csv)
- [MSVC](test-data/msvc.csv)

## 适用范围

适合：

- random / smooth 数值分布；
- 需要 header-only、无第三方运行时依赖；
- 大 n 并行。

不适合：

- 结构化 runs / organ / push；
- 稳定排序；
- 无额外 n 空间；
- 极小 n。

## 当前定位

2.0.0 在 random、runs、sawtooth、near 等模式具有优势；
organ / push 仍落后 TimSort 和 Boost sample_sort。

详细数据见 [benchmarks.md](benchmarks.md)。

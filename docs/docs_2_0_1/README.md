# OblivoneSort 2.0.1

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
- [测试记录](test-record.md)

英文：

- [Design](../docs_2_0_1_EN/design.md)
- [Usage](../docs_2_0_1_EN/usage.md)
- [Test record](../docs_2_0_1_EN/test-record.md)

原始测试数据：

- [13 模式矩阵 (GCC)](test-data/gcc.csv)
- [极限与退化配置](test-data/extreme.csv)

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

2.0.1 在 random、runs 等模式具有明显优势，sawtooth、few、near 5% 在并行下有一定优势；
organ、push 仍落后 TimSort 和 Boost sample_sort。

详细数据见 [测试记录](test-record.md)。

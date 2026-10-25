# OblivoneSort

> **忘一**：忘记自己是一个排序——不去两两比较，而是直接算出每个元素应该在的位置。

OblivoneSort 是一个**算位置**而不是**比大小**的排序库：先统计分布，用前缀和算出每个桶在输出里的精确起点，把元素直接搬到对应位置，再对仍然过大的块递归处理，最后用比较排序兜底。

> **状态：v0.1 原型。** API 未稳定，仅在有限分布上验证过，**请勿用于生产**。

- 语言：C++17，header-only
- 头文件：`include/oblivone/sort.hpp`
- 作者：肖明杰（笔名）
- 许可：Apache-2.0

## 名字

`Oblivone = oblivion + one`，生造词，读作 /əˈblɪviːn/。

**忘一**：忘记自己是一个排序。不去比较，而是直接算出每个元素应该在的位置。

**XSort** 是入口调度器的名字（`X` 表示未知/任意）：运行时根据数据规模、是否已有序、递归深度，决定回退到快排（或用户提供的排序）还是调用 `OblivoneSort`。代码里对应的就是 `oblivone::sort` / `oblivone::sort_by`。

## 它是什么

```
[统计 min/max] → [等宽分 m 桶] → [计数] → [前缀和 = 每块起点]
      → [元素直接落到位置] → [块内递归 / 小段排序] → [比较排序兜底]
```

入口 `XSort` 调度器先判断规模与是否有序：太小或已有序就交给比较排序，否则进入上面的 `OblivoneSort` 核心。

- 不是比较排序：键被映射成 `uint64_t`，桶号由数值直接算出；
- 桶数 `m`（当前段长度），平均每桶 1 个元素；
- 位置由**前缀和**精确给出，不依赖估算；
- 过大的块递归细分，深度超限或段太小交给 `std::sort`。

## 什么时候用 / 别用

**适合**

- 数值键（int / float / double），n ≥ 1e5；
- 均匀、正态、指数、中等偏态、双峰；
- 可以接受额外 `O(n)` 内存、不需要稳定排序。

**不适合**

- n 很小（< 几千）：直接 `std::sort`；
- 极端对抗构造（每层只削掉一小块）：可能退到兜底，最坏接近 `std::sort`；常规分形/离群点已测 2.5~2.8×；
- 内存受限：需要 `3n` 左右的临时空间；
- 需要稳定排序：本库不稳定；
- 字符串 / 自定义可比较类型：请先映射到数值键。

## 快速开始

```cpp
#include <oblivone/sort.hpp>
#include <vector>

int main() {
    std::vector<double> v = {2.0, 4.0, 1.0, 2.1};
    oblivone::sort(v);              // 1.0, 2.0, 2.1, 4.0
}
```

自定义键：

```cpp
struct record { double key; int id; };

std::vector<record> v;
oblivone::sort_by(v, [](const record& r) {
    return oblivone::key_of(r.key);   // 必须单调映射到 uint64_t
});
```

参数：

```cpp
oblivone::options opt;
opt.small_sort  = 64;                 // <= 这个长度直接用 std::sort
opt.max_depth   = 16;                 // 递归深度上限
opt.max_buckets = std::size_t(1) << 16;
oblivone::sort(v, opt);
```

替换兜底排序（默认 `std::sort`，也可以是你自己的并行/稳定排序）：

```cpp
struct my_fallback {
    template <class T, class Cmp>
    void operator()(T* first, T* last, Cmp comp) const { /* ... */ }
};
oblivone::sort(v, opt, my_fallback{});
```

只跑核心（不做规模/有序性判断，供测试和高级用户）：

```cpp
oblivone::oblivone_sort(v.data(), v.size(),
                        [](const double& x) { return oblivone::key_of(x); }, opt);
```

## 构建与测试

```bash
g++ -std=c++17 -O2 -I include tests/test_sort.cpp -o test_sort && ./test_sort
g++ -std=c++17 -O2 -I include bench/bench_sort.cpp -o bench_sort && ./bench_sort 1000000 65536
```

或：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 初步基准

本机：i5-13500H，MinGW g++ 15.2.0，`-O2`，未绑核、未做统计显著性检验，取 3 次最好值。
`n = 1,000,000`，`max_buckets = 65536`，单位 ns/elem：

| 分布 | `std::sort` | OblivoneSort | 加速 |
|---|---:|---:|---:|
| 均匀 | 57.4 | 23.1 | **2.49×** |
| 正态 | 57.8 | 21.3 | **2.71×** |
| 指数 | 56.4 | 27.3 | **2.07×** |
| 帕累托 α=1.5 | 57.1 | 23.4 | **2.45×** |
| 双峰 | 56.6 | 23.0 | **2.46×** |
| 少值（0~9） | 17.7 | 11.0 | **1.62×** |
| 单离群点 | 58.1 | 20.9 | **2.78×** |
| 分形（64 尺度） | 49.9 | 19.0 | **2.62×** |
| 近重复（随机序） | 57.1 | 23.2 | **2.46×** |
| 已排序 | 8.3 | 2.0 | **4.09×** |
| 逆序 | 5.7 | 2.4 | **2.33×** |

`n = 10,000,000`（`max_buckets = 1048576`）：均匀 1.85×、正态 2.84×、指数 1.70×、帕累托 1.93×、双峰 2.71×、少值 1.70×。

**会输的场景**（诚实记录）：

- `n = 10,000` 的“少值”只有 0.94×，略慢于 `std::sort`；
- 对抗分布目前最差 2.46×（n=1e6），更大规模、更极端的构造还没覆盖；
- 尚未与 pdqsort / spreadsort 对比，完整 harness 在路线图里。

## 正确性

- 对拍：随机分布与 `std::sort` 逐元素比较；
- 边界：空、1、2、全相等、两值、已排序、逆序、`INT64_MIN/MAX`、`±Inf`、`-0.0`、`NaN`；
- 对抗：单离群点、64 尺度分形、随机序近重复、99.9%/0.1% 重尾；
- 兜底：强制 `max_depth=1`、`max_buckets=4`、`small_sort=8` 等参数下仍正确；
- `NaN` 映射到最大键，排在最后；
- `-0.0` 与 `+0.0` 视为相等；
- 桶号映射是**单调**的（double 转换 + 正系数乘法 + 截断都单调），所以桶是连续值区间，落位与递归不会打乱全局顺序。

## 算法与复杂度

- 单层 `O(m)`，约 4~5 遍访存；
- 第一层最大桶 ≈ `f_max · (max - min)`；
- 第二层最大桶 ≈ 段内密度对比度，平滑分布通常 1~2 层收敛；
- 最坏 `O(n log n)`（深度保护 + `std::sort` 兜底）；
- 额外内存 `O(n)`。

完整推导、伪代码、边界清单见 [docs/design.md](docs/design.md)。

## 路线图

- [x] v0.1 核心实现 + 对拍测试
- [x] 已排序/逆序快速退出
- [ ] benchmark harness（pdqsort / spreadsort、绑核、统计）
- [ ] 精确整数分桶（128 位）与 MSVC 路径
- [ ] 4 字节计数表（n < 2³²）
- [ ] 无分配 API（调用方提供 scratch）
- [ ] 三平台 CI + ASan/UBSan + libFuzzer
- [ ] 技术博客 / 论文

## 贡献

提交前请：

- 跑 `tests/test_sort.cpp`；
- 不要提交个人邮箱（commit 用 GitHub noreply）；
- 用 DCO 签名：`git commit -s`。

## 许可

Apache-2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

---

*OblivoneSort v0.1 — 忘记排序，直接算序列。*

# OblivoneSort

> **English readers:** see [docs/en/README.md](docs/en/README.md) for usage and
> [docs/en/algorithm.md](docs/en/algorithm.md) for the algorithm overview.
> The English documents are translations and **may contain errors**;
> the Chinese documents ([README.md](README.md), [docs/algorithm.md](docs/algorithm.md),
> [docs/design.md](docs/design.md)) are authoritative.

> **忘一**：忘记自己是一个排序——不去两两比较，而是直接算出每个元素应该在的位置。

OblivoneSort 是一个**算位置**而不是**比大小**的排序库：先统计分布，算出每个元素的下标，用前缀和确定每组的起点，把元素直接搬到对应位置，再对仍然过大的组递归处理，最后用比较排序兜底。

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
[统计 min/max] → [算下标] → [数每组] → [前缀和 = 每组起点]
      → [元素直接落到位置] → [块内递归 / 小段排序] → [比较排序兜底]
```

入口 `XSort` 调度器先判断规模与是否有序：太小或已有序就交给比较排序，否则进入上面的 `OblivoneSort` 核心。

- 不是比较排序：键被映射成 `uint64_t`，下标由数值直接算出；
- 下标数 `m`（当前段长度），平均每组 1 个元素；
- 位置由**前缀和**精确给出，不依赖估算；
- 组 ≥22 递归细分；组 <22 用插入排序（组 = 3 用专用排序）；深度超限才交给 `std::sort`。

默认的三档处理：

| 层 | 条件 | 处理 |
|---|---|---|
| 调度层 | `n ≤ small_top`（默认 48） | 交给兜底（默认 `std::sort`；算术类型 n≤96 用插入） |
| 核心叶子 | 组 < 22 | **插入排序**（double 的 `sort` 用直接 `<` 比较） |
| 核心递归 | 组 ≥ 22 | 继续 `OblivoneSort` |
| 核心小兜底 | 组 < `small_sort`（默认 16） | 交给兜底；默认 22>16，走不到 |
| 嵌套快速路径 | 组 > `scan_threshold`（默认 256） | 先正逆序扫描 + 插入测试，再决定是否递归 |
| 深度兜底 | `depth ≥ max_depth` | 当前组交给兜底 |

> 关键：**`n > 48` 才进 Oblivone；Oblivone 内部 <22 的组用插入排序，
> 不会为它再调一次 Oblivone，也不会走 fallback。**
>
> `oblivone::sort(double*)` 的叶子比较用 `less_double`（NaN 最后，-0.0==+0.0）；
> `sort_by` 传自定义 key 时仍用 key 比较，避免破坏用户的顺序定义。
>
> **double 的自适应键**：排序前扫一遍；全非负用 IEEE 位模式、全非正用位取反，
> 两者都没有符号分支；混号/NaN 回通用 key。全正数据实测约快 **10~18%**，
> 代价是一次 O(n) 分析；混号数据通常前几个元素就放弃，成本接近 0。
>
> 实测（`bench_small`，ns/array）：n<16 时强行“纯 Oblivone”（`leaf_sort=0`，不插入）
> 比插入慢 3~5 倍：n=4 137 vs 25、n=8 305 vs 76、n=16 592 vs 209。
> 所以叶子保留插入；`leaf_sort=0` 只作为实验开关。
>
> 同一套 `key()` 比较下做 2→64K 的扫描：插入在 n≤48 仍最快，
> **n≈64 起 Oblivone 胜出**，n≥256 差距明显；纯 Oblivone 全程未胜出。
> 16~64 这段两者接近，交叉点受测量方法影响，不要当成精确阈值。

### 叶子阈值怎么定

不能拿“纯 Oblivone”去推：生产路径在 `<T` 时必然插入，pure 只能说明“不插入更慢”。
正确做法是把 T 做成参数，扫端到端时间（n=1e6，ns/elem）：

| T | 均匀 | 正态 | 指数 | 少值 | organ |
|---:|---:|---:|---:|---:|
| 8 | 18.9 | 20.3 | 19.6 | 11.7 | 18.2 |
| 16 | 19.3 | 20.3 | 19.6 | 11.4 | 17.5 |
| 24 | 18.7 | 21.0 | 20.0 | 11.8 | 17.3 |
| 32 | 18.8 | 20.5 | 19.9 | 11.8 | 16.7 |
| 48 | 20.2 | 20.3 | 20.2 | 11.8 | 15.7 |

- T=8~32 差异很小，16 是折中（均匀/指数/少值好，organ 略差）；
- T=48 只对 organ 有利，均匀/指数反而变慢；
- 当前取 **22**：按 1:1:1:1 权重算出的中位最优；与 T=24 差 <0.2%，
  T=32 差 <1%，都在噪声内。

补充（n=2..16K、每个 n × 分布 1000 组、同一套 `key()`）：

- **插入 vs quick**：n<32 内插入绝大多数点更快（quick 只在指数 n=5/14 等少数点略胜，差距 <2%）
  → 叶子里用插入；
- **插入 vs Oblivone（固定 T=16）**：
  - 指数：15~16 起 Oblivone 反超；
  - 少值：48 起反超；
  - 均匀/正态：48 时插入仍略胜，64 起 Oblivone 胜；
- **2..128 × T=2..64 网格**（每个 k 取最佳 T）：
  - 稳定交叉点提前到：指数 12、少值 35、正态 47、均匀 48；
  - quick 没有稳定区间；6048 个 (k,T) 组合里 4454~5682 个由 Oblivone 胜；
  - k≥48 的最佳 T 中位数：均匀 25、正态 20、指数 34、少值 9；
- 16K 时 Oblivone 比 quick 快 3.6~5.3×；
- 阈值是分布相关的；当前 `leaf_sort=22`（1:1 加权中位最优），
  指数偏多可降到 16~20，均匀/正态可保持 22~32。

按 **1:1:1:1** 加权（没有可靠的真实分布概率）：

- k=2..32 插入占优，**k=33..128 Oblivone 占优**；
- k=16..128 平均：插入 2269、quick 2248、Oblivone（当前 leaf=22）**1268 ns/组**
  → 相对 quick **1.77×**、相对插入 **1.79×**；
- 最佳 T 中位 **22**；生产配置 `small_sort=16`、`leaf_sort=22` 的期望约 1268，
  T=24 是 1266，差 <0.2%（噪声内）；
- 权重敏感性：指数偏重时交叉点提前到 24，其余权重在 33~35；
  平均加速 1.76~2.05×。

> **权重提示**：以上加权结果基于 **1:1:1:1**（没有真实分布概率）。
> 如果后续能统计出真实比例，运行
> `py -3 bench/weighted.py build/grid_2_128.csv w_uniform w_normal w_exp w_few`
> 重新计算 T；当前先取 `leaf_sort=22`。

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

    oblivone::xsort(v);   // XSort 调度器（显式名字）
    oblivone::sort(v);    // 标准别名，等价
}
```

自定义键：

```cpp
struct record { double key; int id; };

std::vector<record> v;
oblivone::xsort_by(v, [](const record& r) {
    return oblivone::key_of(r.key);   // 必须单调映射到 uint64_t
});
```

参数：

```cpp
oblivone::options opt;
opt.small_sort  = 16;                 // 核心组 < 这个值交兜底（默认 16）
opt.small_top   = 48;                 // 顶层 n ≤ 这个值交兜底（默认 48）
opt.leaf_sort   = 22;                 // 组 < 这个值插入（默认 22）
opt.scan_threshold = 256;             // 嵌套块 > 此值先做有序扫描/插入测试
opt.max_depth   = 16;                 // 递归深度上限
opt.max_buckets = std::size_t(1) << 16;
opt.bucket_cap  = 0;                  // 0=自动；>0 强制每层 cap（调参用）
oblivone::sort(v, opt);
```

替换兜底排序（默认：算术小段用插入，其余 `std::sort`；也可以是你自己的并行/稳定排序）：

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

## API 命名

| 名字 | 角色 |
|---|---|
| `oblivone::xsort` / `xsort_by` | XSort 调度器（项目专有名） |
| `oblivone::sort` / `sort_by` | 标准别名，按 `std` 习惯调用 |
| `oblivone::oblivone_sort` | OblivoneSort 核心（项目专有名） |
| `oblivone::options` / `key_of` / `default_fallback` | 通用名 |
| `oblivone::detail::*` | 内部实现，不属于公共 API |

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

> 这一节只对比 `std::sort` 和朴素 radix。对 pdqsort/ska/TimSort 的对比见
> 下面“已知排序对比”；**Oblivone 不是全局最快**。

| 分布 | `std::sort` | LSD radix | OblivoneSort | 加速（vs std） |
|---|---:|---:|---:|---:|
| 均匀 | 56.8 | 41.5 | 19.1 | **2.97×** |
| 正态 | 57.2 | 38.3 | 20.1 | **2.85×** |
| 指数 | 55.9 | 39.2 | 19.3 | **2.90×** |
| 帕累托 α=1.5 | 57.1 | 41.2 | 19.2 | **2.98×** |
| 双峰 | 57.3 | 41.0 | 21.7 | **2.64×** |
| 少值（0~9） | 17.0 | 39.9 | 11.7 | **1.45×** |
| 单离群点 | 57.0 | 38.5 | 18.1 | **3.14×** |
| 分形（64 尺度） | 49.3 | 38.8 | 19.6 | **2.52×** |
| 近重复（随机序） | 56.7 | 41.8 | 21.0 | **2.69×** |
| 已排序 | 7.9 | 38.4 | 2.0 | **3.92×** |
| 逆序 | 6.0 | 38.1 | 2.3 | **2.55×** |

## 标准模式（Bentley–McIlroy）

`bench_standard.exe 1000000 5 256`，1 次预热 + 5 次中位数、绑核，单位 ms：

| 模式 | `std::sort` | OblivoneSort | 加速 |
|---|---:|---:|---:|
| random | 69.2 | 18.4 | 3.77× |
| sorted | 8.7 | 2.1 | 4.07× |
| reverse | 6.5 | 2.4 | 2.68× |
| organ pipe | 33.5 | 12.6 | 2.67× |
| sawtooth | 18.0 | 9.7 | 1.85× |
| runs (64) | 56.7 | 21.2 | 2.67× |
| all equal | 7.0 | 2.1 | 3.35× |
| few unique | 13.9 | 10.6 | 1.32× |
| near 0.1% | 8.6 | 1.9 | **4.62×** |
| near 1% | 8.8 | 2.0 | **4.51×** |
| near 5% | 9.3 | 2.4 | **3.85×** |
| push front | 42.3 | 10.5 | 4.04× |
| push back | 47.8 | 14.9 | 3.21× |

- **近有序回归**：修复前 near 0.1%/1%/5% 只有 0.47/0.57/0.50×；
- 修复方式：调度器加“带预算的插入排序”快速路径（预算 `n/8` 次搬移），
  完成就返回，超预算中止后继续 Oblivone；
- **嵌套扫描**：`scan_threshold=256` 的嵌套正逆序扫描 + 插入测试，
  把 organ 从 1.62× 提到 2.67×、push 从 2.67/2.77× 提到 4.04/3.21×；
- 1e7 的 near 0.1%/1%/5% 约 4.4/6.3/3.6×（单次，有波动）；
- 仍偏弱：`few unique` 1.32×、`sawtooth` 1.85×；
- 绝对时间受绑核、系统负载影响，和上面“初步基准”的 ns/elem 不要直接混比。

## 已知排序对比（n=1e6）

同一批数据、同一台机器、绑核，中位数 3 次；官方实现：

- `std::sort`（introsort）
- `std::stable_sort`
- **pdqsort**（orlp/pdqsort）
- **Boost.Spreadsort**（float_sort）
- **ska_sort**
- **TimSort**（gfx/cpp-TimSort）
- OblivoneSort

| 模式 | 最快 | 最快（ms） | Oblivone（ms） | Oblivone/最快 |
|---|---|---:|---:|---:|
| random | **Oblivone** | 18.66 | 18.66 | 1.00 |
| sorted | TimSort | 0.63 | 2.46 | 3.92 |
| reverse | TimSort | 1.09 | 2.97 | 2.74 |
| organ | **TimSort** | 1.07 | 13.59 | **12.70** |
| sawtooth | pdqsort | 3.66 | 8.21 | 2.25 |
| runs | ska_sort | 21.66 | 30.01 | 1.39 |
| equal | TimSort | 0.60 | 1.74 | 2.88 |
| few | pdqsort | 4.43 | 7.63 | 1.72 |
| near 0.1% | TimSort | 0.71 | 2.23 | 3.15 |
| near 1% | TimSort | 1.59 | 2.53 | 1.59 |
| near 5% | **Oblivone** | 3.06 | 3.06 | 1.00 |
| push front | **TimSort** | 1.15 | 9.70 | **8.45** |
| push back | **TimSort** | 1.09 | 14.89 | **13.69** |

n=1e7：random（217.5 vs ska 278.0）、runs（235.7 vs ska 249.5）、
near5（33.9 vs TimSort 48.4）仍由 Oblivone 胜；
organ 158.1 vs TimSort 13.1（12.1×）、push 121.7/155.9 vs 13.5/13.4（9.0/11.6×）；
sorted 33.8 vs TimSort 6.5（5.2×）、few 101.4 vs pdq 51.5（1.97×）、
sawtooth 86.6 vs pdq 73.7（1.18×）。

> 1e6 胜场：TimSort 8/13、pdqsort 2/13、ska_sort 1/13、Oblivone 2/13。
> 1e7 胜场：TimSort 8/13、pdqsort 2/13、Oblivone 3/13。
> 单次绑核结果，organ/push/runs 的绝对时间有波动。

结论（诚实版）：

- **Oblivone 不是“已知最快”**；random / near5 稳定领先，runs 视 run 分布
  （1e7 领先，1e6 与 pdq 接近）；
- **TimSort 在结构化/近有序/push 模式上仍然明显更快**（organ/push 差 8~13 倍）；
- **pdqsort 在 few/sawtooth 上更快**；
- Spreadsort 没有拿过第一，但 sorted/equal 很接近 TimSort；
- 所以对外只能写“在平滑数值分布、大 n 上有竞争力”，不能写“最快”。

> 这组对比也说明：如果目标是“通用最快”，应该把 TimSort/pdqsort 作为调度器的候选；
> 如果坚持 Oblivone 核心，至少要为“少 run / 结构化”加一个回退策略。

## 三档阈值（实测）

块大小 `k`、随机数据、n=1e6、同一套 `key` 比较，单位 ns/elem：

| k | 插入 | 快排 | Oblivone | 最快 |
|---:|---:|---:|---:|---|
| 4 | **6.7** | 7.2 | 17.1 | 插入 |
| 8 | **10.5** | 11.3 | 14.0 | 插入 |
| 12 | **13.3** | 13.9 | 14.0 | 插入 |
| 16 | 15.5 | 15.9 | **14.6** | Oblivone |
| 24 | 19.6 | 21.7 | **16.3** | Oblivone |
| 32 | 23.5 | 24.1 | **13.7** | Oblivone |
| 48 | 29.0 | 27.1 | **14.3** | Oblivone |
| 64 | 34.7 | 29.5 | **13.4** | Oblivone |
| 128 | 57.7 | 34.3 | **12.7** | Oblivone |
| 256 | 103.3 | 38.7 | **12.6** | Oblivone |

结论：

- **k ≤ 12**：插入排序最快；
- **k ≥ 16**：Oblivone 核心已经更快；
- **快排没有最优区间**：它对插入的优势要到 k≈48 才出现，但那时 Oblivone 已经快一倍以上；
- 叶子阈值取 16：12~15 这一小段 Oblivone 只快约 5%，用插入更简单；
- 所以默认 `small_sort = 16`，快排只作为**深度兜底**（对抗数据）和“用户注入的 fallback”使用。

`small_sort` 16 vs 64（同批数据，n=1e6，ns/elem）：

| 分布 | 16 | 64 |
|---|---:|---:|
| 均匀 | **19.4** | 22.0 |
| 指数 | **20.1** | 25.2 |
| 正态 | 20.9 | 20.8 |
| 双峰 | 24.1 | 24.5 |


LSD 基数排序固定做 8 趟（64-bit key），无法利用少值/已排序，所以在这两类上明显吃亏；`pdqsort` 和 Boost.Sort `spreadsort` 由 `__has_include` 自动检测，把 `pdqsort.h` 放进 `third_party/` 或安装 Boost 后会自动加入对比（见 [bench/README.md](bench/README.md)）。

`n = 10,000,000`（`max_buckets = 1048576`）：均匀 2.52×、正态 3.04×、指数 2.39×、帕累托 2.57×、双峰 3.00×、少值 1.73×、单离群点 2.95×、分形 2.76×、近重复 1.98×。

### 计数表上限（max_buckets 自适应）

| n | 内部上限 | 表大小 | 说明 |
|---:|---:|---:|---|
| ≤ 2e6 | 64K | 256KB | n=1e6 比 1M 快 10~20% |
| > 2e6 | 1M | 4MB | n=1e7 对分形/重尾更稳 |

这里的 K 是 1024：64K = **65,536**，1M = **1,048,576**，不是 16000 或 1000000。

`max_buckets` 仍然可以再往下压；0 按 1 处理。

## 覆盖矩阵（n=1e6）

`bench/bench_matrix.cpp`，中位数 3 次，`small_sort=16`。

**类型 × 均匀/少值**（加速 vs `std::sort`）：

| 类型 | 均匀 | 少值 |
|---|---:|---:|
| int32 | 3.21× | 2.21× |
| int64 | 2.50× | 1.73× |
| uint32 | 3.31× | 2.49× |
| uint64 | 2.55× | 1.63× |
| float | 3.18× | 1.71× |
| double | 3.20× | 1.42× |

**double × 分布**（加速 vs `std::sort`）：

| 分布 | 加速 | 分布 | 加速 |
|---|---:|---|---:|
| 均匀 | 3.20× | 双峰 | 2.89× |
| 正态 | 2.66× | 少值 | 1.42× |
| 指数 | 2.86× | organ pipe | 1.59× |
| 帕累托 | 2.82× | 锯齿 | 1.70× |
| 近重复 | 2.71× | 分段 runs | 2.63× |
| 全相等 | 3.27× | 已排序/逆序 | 4.19×/2.54× |

## 适用范围（实测）

同一套数据，改变 n：

| n | 平滑分布 | organ pipe | 少值 | 锯齿 |
|---:|---:|---:|---:|---:|
| 1e4 | 1.7~2.3× | **0.77×** | 1.05× | **0.89×** |
| 1e5 | 2.0~2.6× | 1.34× | 1.25× | 2.37× |
| 1e6 | 2.7~3.2× | 1.59× | 1.42× | 1.70× |

- **n ≥ 1e5**：所有测过的分布都赢，最低约 1.25×（少值，n=1e5）；
- **n ≈ 1e4**：平滑分布仍赢 1.7~2.3×，但 **organ pipe 0.77×、锯齿 0.89×、uint64 少值 0.92×**；
  这几类“结构化小数据”用 `std::sort` 更稳；
- 已排序/逆序：调度层直接返回/反转，任何 n 都接近 0；
- 所以顶层**不能按 n 一刀切回退**——那样会丢掉平滑分布 1.7~2.3× 的收益；
  要回退应该看结构（长 run、重复值），不是只看规模。

**会输的场景**（诚实记录）：

- `n = 1e4` 的 organ pipe：**0.77×**；
- `n = 1e4` 的锯齿：**0.89×**；
- `n = 1e4` 的 uint64 少值：**0.92×**；
- 少值（0~9）：1.05~1.73×，是收益最低的分布之一；
- `n ≥ 1e5` 内最差约 1.25×（少值，n=1e5）；
- **对已知排序**：Timsort 在结构化/近有序/push 上更快（organ 9.4×、push 8~13×）；
  pdqsort 在 few/sawtooth 上更快；详见下面“已知排序对比”；

## 正确性

- 差分 fuzz：随机类型（6 种）× 分布 × 规模 × 参数，累计 100 万+ 组，与 `std::sort` 逐元素对拍；
- 标准模式：Bentley–McIlroy 的 random/sorted/reverse/organ/sawtooth/runs/equal/
  few/near-sorted/push-front/push-back；
- 对拍：随机分布与 `std::sort` 逐元素比较；
- 边界：空、1、2、全相等、两值、已排序、逆序、`INT64_MIN/MAX`、`±Inf`、`-0.0`、`NaN`；
- 对抗：单离群点、64 尺度分形、随机序近重复、99.9%/0.1% 重尾、近有序；
- 兜底：强制 `max_depth=1`、`max_buckets=4`、`small_sort=8` 等参数下仍正确；
- `NaN` 映射到最大键，排在最后；
- `-0.0` 与 `+0.0` 视为相等；
- 下标映射是**单调**的（double 转换 + 正系数乘法 + 截断都单调），所以同一下标对应连续值区间，落位与递归不会打乱全局顺序。

## 算法与复杂度

- 单层 `O(m)`，约 4~5 遍访存；
- 第一层最大组 ≈ `f_max · (max - min)`；
- 第二层最大组 ≈ 段内密度对比度，平滑分布通常 1~2 层收敛；
- 最坏 `O(n log n)`（深度保护 + `std::sort` 兜底）；
- 额外内存：典型 `O(n)`；最坏 `O(n · max_depth)`（每层计数表同时存活）。

完整推导、伪代码、边界清单见 [docs/design.md](docs/design.md)；
不涉及代码、只讲逻辑的版本见 [docs/algorithm.md](docs/algorithm.md)；
对“为什么结构化数据会输”的系统讨论见 [docs/weakness.md](docs/weakness.md)。

## 路线图

- [x] v0.1 核心实现 + 对拍测试
- [x] 已排序/逆序快速退出
- [ ] benchmark harness（pdqsort / spreadsort、绑核、统计）
- [ ] 精确整数下标（128 位）与 MSVC 路径
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

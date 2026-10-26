# 基准

## 构建与运行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bench_sort 1000000 65536
```

或者直接编译：

```bash
g++ -std=c++17 -O2 -I ../include bench_sort.cpp -o bench_sort
./bench_sort 1000000 65536
```

参数：`n`（元素个数）、`max_buckets`（每层桶数上限）。

## 对比对象

- `std::sort`：内省排序基线；
- **LSD 基数排序**：对 64 位键固定 8 趟、每趟 8 位，分布排序最直接的对手；
- `oblivone::sort`：本库；
- `pdqsort` 和 Boost.Sort `spreadsort`：**头文件存在时**自动加入。

## 启用 pdqsort / spreadsort

基准用 `__has_include` 检测，没有就跳过，仓库保持零依赖：

- **pdqsort**：从 <https://github.com/orlp/pdqsort> 下载 `pdqsort.h`，
  放到 `third_party/pdqsort.h`；CMake 检测到 `third_party/` 会自动加 include 路径。
- **Boost.Sort**：安装 Boost，`<boost/sort/spreadsort/spreadsort.hpp>` 会被自动识别。

## 2/3 元素：bench_tiny

```powershell
.\build\bench_tiny.exe 5000 5
```

5000 组，单位 ns/组：

| n | 插入 | 专用 | 插入/专用 |
|---:|---:|---:|---:|
| 2 uniform | 5.70 | 7.41 | 0.77（插入更快） |
| 2 normal | 4.77 | 7.12 | 0.67（插入更快） |
| 3 uniform | 14.38 | 10.65 | **1.35×** |
| 3 normal | 14.00 | 10.33 | **1.36×** |
| 3 exponential | 14.22 | 9.20 | **1.55×** |
| 3 few | 13.97 | 9.39 | **1.49×** |

结论：**2 元素保留插入；3 元素改用专用 `sort3`**。

### n=2 的 100 万组对比

```powershell
.\build\bench_sort2.exe 1000000 5
```

| 分布 | 插入 | move | swap | cached | branchless | raw `<` |
|---|---:|---:|---:|---:|---:|---:|
| uniform | 9.36 | 9.53 | 9.62 | 9.68 | 10.16 | 4.45 |
| normal | 9.38 | 9.59 | 9.67 | 9.40 | 10.01 | 4.42 |
| exponential | 5.64 | 5.61 | 5.52 | 5.37 | 5.92 | 4.44 |
| few | 6.22 | 6.28 | 6.36 | 5.88 | 6.51 | 4.47 |

结论：**compare + swap 本身不慢**；各写法差异在 ±5% 内，主要是噪声。
`raw <` 4.4 vs key 9.4 说明成本主要在 `key()`（double→uint64）转换，
不在比较/交换。之前 0.5× 的结论是小样本测量偏差；n=2 继续用插入排序。

## 只第一层桶中桶 + 原核心：bench_offset_first（失败）

```powershell
.\build\bench_offset_first.exe 10000000 2 4
```

规则：

- 只有最外层做“4 子桶 + 4 张 offset 表（合计 n）”；
- offset 槽内的组，回到原 `sort_range` 核心，不再用 `std::sort`；
- 组 <22 时仍走原叶子插入/3 元素。

1e6：

| MT | random | few | organ |
|---:|---:|---:|---:|
| 4 | 1.65× | 1.63× | 2.56× |
| 16 | 1.51× | 1.73× | 2.44× |
| 64 | 1.69× | 1.75× | 1.87× |
| 256 | 2.20× | 1.44× | 2.15× |

1e7、MT=4：

| random | few | organ |
|---:|---:|---:|
| 1.58× | 1.65× | 2.38× |

比“组内 std::sort”版好一些（2.7~4.8× 降到 1.5~2.6×），但仍然全面更慢。

原因：

- n 长度 offset 表 + 一次 n 前缀和，仍是主要成本；
- 1e7 时表 40MB，缓存更差；
- 原核心只处理 offset 槽内的小组，救不回顶层 n 表的开销；
- few 的 offset 槽内重复多，叶子插入仍然要做。

结论：**只第一层做也不划算；n 表成本 > 省下的递归。**

## 4 子桶 + 4 张 offset 表：bench_offset_tables（失败）

```powershell
.\build\bench_offset_tables.exe 1000000 3 4
```

严格按“4 个子桶、4 张 offset 表、总空间 = n”实现：

- 没有全局 `cnt[B]`；
- 4 张表长度分别是 `c[0]..c[3]`，合计 n；
- 每张表独立计数、前缀和；
- `pos = base[b] + offset`；
- 同一个 offset 内的元素最后再排。

1e6：

| MT | random | few | organ |
|---:|---:|---:|---:|
| 4 | 2.69× | 4.66× | 2.99× |
| 16 | 2.47× | 4.79× | 2.70× |
| 64 | 2.09× | 4.36× | 2.30× |
| 256 | 2.32× | 4.10× | 2.23× |
| 1024 | 2.47× | 4.57× | 2.05× |

全部慢 2~5 倍。

原因不是碰撞处理，而是“表总大小 = n”本身：

- 当前 core 的表是 `B = 64K`，256KB；
- offset 表是 n，1e6 时 4MB，16 倍；
- 多一次 offset 计数 + 一次 n 长度前缀和；
- 每元素多一次浮点 offset 计算；
- 最后还要排 offset 槽内元素；
- few 的 offset 槽内重复/碰撞多，额外排序最贵。

## 桶中桶（固定 16 路）：bench_sub_bucket（失败）

```powershell
.\build\bench_sub_bucket.exe 1000000 3 4096 16
```

规则：

- 段 > `CMax`：固定切成 `MT` 个子桶；
- 段 ≤ `CMax`：回到当前自适应桶数 `B = min(m, cap)`；
- 子桶间隔应为 `(max-min)/MT`，不是 `(MT-1)`。

1e6、重复 3 次：

| 模式 | 当前 core | CMax=4096, MT=16 | 倍数 |
|---|---:|---:|---:|
| random | 20.0 ms | 38.4 ms | 1.92× |
| few | 10.1 ms | 16.9 ms | 1.68× |
| organ | 10.4 ms | 33.6 ms | 3.23× |

扫 MT/CMax：

| 配置 | random | few | organ |
|---|---:|---:|---:|
| CMax=4096, MT=4 | 2.55× | 2.10× | 4.38× |
| CMax=4096, MT=64 | 1.69× | 1.46× | 2.50× |
| CMax=4096, MT=256 | 1.57× | 1.61× | 2.68× |
| CMax=4096, MT=1024 | 1.29× | 1.42× | 2.34× |
| CMax=65536, MT=1024 | 1.34× | 1.47× | 2.52× |

MT=4 在 1e7 更差：random 2.98×、few 2.11×、organ 5.12×。

1e7 同样：CMax=4096/MT=16 时 random 2.06×、few 1.69×、organ 3.52×；
即使用 MT=1024，random 仍 1.28×、few 1.33×、organ 2.21×。

结论：**固定 16 路桶中桶更慢，不采用。**

- 当前 `B=min(m, cap)` 对 1e6 基本一层就分到十几元素；
- 桶中桶要多 2~3 层，每层都是一次 count+scatter；
- 表小（16/1024）能进缓存，但省下的表访问抵不过多出来的整数组遍历；
- organ 还叠加了“破坏 run”的损失。

## 纯 double 桶映射：bench_double_map（失败）

```powershell
.\build\bench_double_map.exe 10000000 3
```

同一份 random 数据：

- key core：当前 `oblivone_sort`，uint64 key + 整数桶号；
- double core：完全不用 key，直接 `(x-mn)*scale` 算桶号。

| n | key core | double core | 结果 |
|---:|---:|---:|---:|
| 1e6 | 17.8~20.3 ms | 19.0~24.4 ms | double 慢 6~23% |
| 1e7 | 191~196 ms | 279~286 ms | **double 慢约 46%** |

double core 统计：`nodes=410, sum_m=1.0009n, max_depth=1, fallback=0`，
分布正常，不是递归/桶分布 bug。

结论：

- “去掉 key 转换”并不等于更快；
- key 路径的 `(k-mn)*scale` 是整数减法 + 一次 int→double，
  比纯 double 的 `(x-mn)*scale` 更快；
- 之前 32.8% 的“key 转换开销”是对 `uint64` 直排的上界，
  真正换成 double 桶映射后，浮点减法/乘法的延迟把它吃掉了；
- **不集成**，保留当前 key 路径。

## 自适应 key 模式：bench_key_modes

```powershell
.\build\bench_key_modes.exe 1000000 5
```

`sort(double*)` 第一次扫描后选择：

| 数据 | key 模式 | 转换 |
|---|---|---|
| 全非负（含 +0） | `key_nonneg` | 直接取 IEEE 位模式，无符号分支 |
| 全非正（含 -0） | `key_nonpos` | 位模式取反，无符号分支 |
| 混号 / NaN | 通用 `to_key` | NaN 判断、-0 归一、符号分支 |

1e6 结果（单次有波动）：

| 数据 | 默认 key | 专用 key | 自适应 | 自适应收益 |
|---|---:|---:|---:|---:|
| 全非负 | 19.2~23.3 ms | 15.4~15.8 ms | 17.2~19.1 ms | **约 10~18%** |
| 全非正 | 19.0~19.9 ms | 15.3~16.5 ms | 17.5~20.3 ms | 约 0~12% |
| 混号 | 18.0~18.6 ms | — | 17.9~18.6 ms | 基本不变 |

- 专用 key 的理论上限是全非负/非正 **快约 20%**；
- 但自适应要先扫一遍，分析成本约 2~4 ms/1e6，吃掉一部分收益；
- 混号数据第一次扫描很快发现两种符号，几乎零成本；
- 全正数据（时间戳、大小、价格）是主要受益场景。

## 去掉 double→uint64 转换：bench_nokey

```powershell
.\build\bench_nokey.exe 1000000 3
```

同一批数据：double 排序（桶映射 key，叶子 direct double）vs 预先转成 `uint64_t` 键后直排（排序期零转换）。

| 算法 | double | uint64 直排 | 差值 |
|---|---:|---:|---:|
| std::sort | 93.2 ms（key 比较器） | 53.0 ms | key 比较器使 std 慢 76% |
| Oblivone | 18.65 ms（key 叶子） | 14.04 ms | **key 转换占 32.8%** |
| Oblivone | 18.68 ms（double 叶子） | 14.04 ms | 叶子直比只省 0.2% |

- raw `double` `<` 排序是 58.0 ms，`std + key` 是 93.2 ms：key 在每次比较里被调用，成本很高；
- 叶子改成 direct double 后，整排序几乎不变（18.65 → 18.68，噪声级）：
  **叶子比较只占 key 调用的一小部分**，大头在 min/max、计数、落位；
- Oblivone 的 18.65 → 14.04 ms 是“排序期完全不转换”的上界；
- 真正要省这笔钱，需要**预计算一次键**并让排序搬运 `(key, value)` 或维护键数组；
  这会多 8n 字节和更大搬移量，实测 pair 反而慢 6.6%（见下节）；
- 整数/无符号类型本身 key 是恒等映射，没有这笔开销。

## 叶子 double 比较：bench_leaf_less

```powershell
.\build\bench_leaf_less.exe 1000000 5
```

1,000,000 个数组，ns/数组：

| n | 分布 | 插入 key | 插入 double | 专用 key | 专用 double |
|---:|---|---:|---:|---:|---:|
| 2 | uniform | 6.27 | 5.15 | 9.76 | **4.54** |
| 2 | normal | 6.25 | 5.29 | 9.61 | **4.60** |
| 2 | exponential | 6.51 | 5.20 | 5.60 | **4.48** |
| 2 | few | 6.68 | 5.17 | 6.07 | **4.48** |
| 3 | uniform | 14.46 | 11.50 | 12.96 | **9.48** |
| 3 | normal | 14.43 | 11.38 | 13.21 | **9.31** |
| 3 | exponential | 14.17 | 11.54 | 10.47 | **9.64** |
| 3 | few | 14.89 | 11.53 | 11.22 | **9.48** |

结论：

- double 路径下，n=2 用“一次比较 + 条件交换”最快（4.5 ns），
  n=3 用 `sort3_less` 最快（9.3~9.6 ns）；
- 通用 `sort_by`（自定义 key）仍走 key 版本，不强行用 double 比较；
- `oblivone::sort(double*)` 会自动走 double 叶子比较。

## 预计算键：bench_pair

```powershell
.\build\bench_pair.exe 1000000 3
```

把 `key()` 预计算一次，排序 `(key, value)` 对，排序期零转换：

| 方案 | 1e6 时间 | ns/elem |
|---|---:|---:|
| double + key() | 17.99 ms | 17.99 |
| (key,value) 对 | 19.17 ms | 19.17 |
| 只排 uint64 键（上界） | 14.32 ms | 14.32 |

- 去掉转换省了约 **3.77 ms**；
- 但每个元素从 8 字节变成 16 字节搬移，额外成本约 **4.95 ms**；
- 净结果：pair 比当前 double 路径慢 **6.6%**；
- 结论：**预计算键“一直使用”在普通 `(key,value)` 布局下不划算**；
  当前“重复 key() + 8 字节搬移”反而更好。
- 如果键本来就有（例如直接排 uint64），那就不存在这笔转换成本。

## 混合实验：bench_hybrid（TEST ONLY）

```powershell
.\build\bench_hybrid.exe 10000000 2
```

规则：

- 块 > T：用 `oblivone_sort` 核心递归分桶（含嵌套）；
- 块 ≤ T：不处理；
- 全部处理完，对整数组跑 `gfx::timsort`；
- 与纯 TimSort 对比。

1e7、绑核，ratio = hybrid / 纯 TimSort：

| 模式 | 纯 TimSort | T=64 | T=8192 | T=n |
|---|---:|---:|---:|---:|
| random | 1008.2 ms | **254.2 ms (0.252)** | 631.7 (0.627) | 1033.0 (1.025) |
| few | 183.0 ms | 103.1 ms (0.563) | 102.4 (0.559) | 203.8 (1.113) |
| sorted | 7.54 ms | 159.3 ms (21.1) | 85.1 (11.3) | 25.6 (3.40) |
| organ | 12.42 ms | 201.9 ms (16.3) | 87.3 (7.03) | 31.5 (2.54) |
| push front | 13.18 ms | 156.6 ms (11.9) | 86.2 (6.54) | 33.6 (2.55) |
| near 5% | 40.20 ms | 175.2 ms (4.36) | 116.8 (2.90) | 60.1 (1.50) |

1e6 趋势一致：random 最好 T≈64（0.255×），few 在 T≈64~128（0.56~0.57×），
sorted/organ/push 在所有 T 下都显著慢于纯 TimSort。

结论：

- T 的适用范围只在 **random / few**；
- random：T 在 **32~1024** 都接近最优，**T=64** 最稳；T≥2048 开始退化；
- few：T 范围很宽（0~8192 都在 0.55~0.65），T≈64~128；
- **结构化数据不要用**：T=0~8192 全部比纯 TimSort 慢 2.5~41×；
  T=n 也慢 1.5~3.4×，因为核心仍分配了 `buf`；
- 对 random，hybrid 仍比**纯 Oblivone** 慢（1e7：254 vs 217.5 ms；1e6：22.8 vs 18.7 ms）；
- hybrid **不稳定**：Oblivone 分桶已经打乱相等元素顺序，最后跑 TimSort 也恢复不了；
  需要稳定排序时不能用它替代 TimSort。

## 已知排序对比：bench_known

需要官方实现（不随仓库分发）：

- pdqsort：`pdqsort.h`（orlp/pdqsort）
- ska_sort：`ska_sort.hpp`（skarupke/ska_sort）
- TimSort：`gfx/timsort.hpp`（gfx/cpp-TimSort，需要 C++20）
- Boost.Spreadsort：Boost 1.89+ 的 `boost/sort/spreadsort/spreadsort.hpp`

本机放在 `build/third_party/`（被 `.gitignore` 忽略）。CMake 会自动探测
`build/third_party/` 和 `BOOST_ROOT`，也可以直接：

```powershell
cmake --build build --config Release --target bench_known
```

或手工编译（路径按你的机器替换）：

```powershell
g++ -std=c++20 -O2 -I include `
  -I build/third_party/pdqsort `
  -I build/third_party/ska_sort `
  -I build/third_party/cpp-TimSort/include `
  -I D:/VSC_TOOL/boost-1.89.0 `
  bench/bench_known.cpp -o build/bench_known.exe
```

MSVC 下 `ska_sort.hpp` 会触发编译器内部错误（C1001），`bench_known` 在 MSVC
会自动跳过 ska 列；g++ 正常。

1e6、绑核、中位数 3 次：

| 模式 | 最快 | 最快 ms | Oblivone ms | Oblivone/最快 |
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

结论：**Oblivone 不是已知最快**；1e6 胜场 TimSort 8/13、pdqsort 2/13、
ska_sort 1/13、Oblivone 2/13。1e7 时 Oblivone 拿下 random/runs/near5，
共 3/13；TimSort 仍是 8/13。

> 1e7：random 217.5 vs ska 278.0、runs 235.7 vs ska 249.5、near5 33.9 vs Tim 48.4；
> organ 158.1 vs Tim 13.1（12.1×）、push 121.7/155.9 vs 13.5/13.4（9.0/11.6×）。
> 单次结果，organ/push/runs 有波动。

## 标准模式：bench_standard

```powershell
.\build\bench_standard.exe 1000000 5 256
```

Bentley–McIlroy 风格的模式，1 次预热 + 5 次中位数，绑核；单位 ms：

| 模式 | std | Oblivone | 加速 |
|---|---:|---:|---:|
| random | 69.2 | 18.4 | 3.77× |
| sorted | 8.7 | 2.1 | 4.07× |
| reverse | 6.5 | 2.4 | 2.68× |
| organ | 33.5 | 12.6 | 2.67× |
| sawtooth | 18.0 | 9.7 | 1.85× |
| runs | 56.7 | 21.2 | 2.67× |
| equal | 7.0 | 2.1 | 3.35× |
| few | 13.9 | 10.6 | 1.32× |
| near 0.1% | 8.6 | 1.9 | **4.62×** |
| near 1% | 8.8 | 2.0 | **4.51×** |
| near 5% | 9.3 | 2.4 | **3.85×** |
| push front | 42.3 | 10.5 | 4.04× |
| push back | 47.8 | 14.9 | 3.21× |

- 近有序在加“带预算插入”之前只有 0.47~0.57×；现在是 3.9~4.6×；
- `scan_threshold=256` 的嵌套扫描把 organ 从 1.62× 提到 2.67×、
  push 从 2.67/2.77× 提到 4.04/3.21×；
- n=1e7 时 near 0.1%/1%/5% 约 4.4/6.3/3.6×（单次，有波动）；
- 仍偏弱：few 1.32×、sawtooth 1.85×。

## 覆盖矩阵：bench_matrix

```powershell
.\build\bench_matrix.exe 1000000 3 16 > matrix.csv
```

参数：`n`、`reps`、`small_sort`（默认 16）。

输出 CSV：`type,dist,n,std_ns,radix_ns,oblivone_ns,speedup`。

- 类型：`int32/int64/uint32/uint64/float/double`
- 分布：`uniform/normal/exponential/pareto/bimodal/few/sorted/reverse/organ/sawtooth/runs/equal/near_dup`
- 基线：`std::sort`、LSD radix（8 趟）、OblivoneSort
- 计时：取 `reps` 次的中位数

它的用途是“找输的场景”，不是刷分；`organ`、`few`、`sawtooth` 这些就是靠它暴露出来的。

## n=2..16K：插入 vs quick vs Oblivone（每个 n × 分布 1000 组）

```powershell
.\build\bench_leaf.exe 5
```

尺寸：2..16 步 1；32..128 步 16；256..1024 步 128；
2048..4096 步 1024；8192..16384 步 4096。
每个 n × `uniform/normal/exponential/few` 各 1000 组，
三种算法都用同一套 `key()`，取 5 次平均。插入 n>1024 未测（O(n²)）。

关键交叉点（ns/组）：

| 分布 | Oblivone 反超插入 | Oblivone 反超 quick | 16K quick | 16K Oblivone | 16K 加速 |
|---|---:|---:|---:|---:|---:|
| 均匀 | 64 | 48 | 1,147,196 | 281,461 | 4.08× |
| 正态 | 64 | 32 | 1,144,186 | 321,706 | 3.56× |
| 指数 | 16 | 9 | 1,178,412 | 223,609 | 5.27× |
| 少值 | 48 | 32 | 632,509 | 167,026 | 3.79× |

- n=48 时均匀/正态插入仍略胜（988/1008 vs 1100/1099），n=64 起 Oblivone 胜；
- 指数在 15~16 就反超，少值在 48 反超；
- n<32 内插入绝大多数点更快；quick 只在指数 n=5/14 等少数点略胜，差距 <2%。

结论：

- 叶子算法用插入（vs quick）；
- 阈值是**分布相关**的：指数 ~16，少值 ~48，均匀/正态 ~64；
- 当前取 `leaf_sort=22`（1:1 加权中位最优）；指数偏多可降到 16~20，平滑保持 22~32；
- n≥1024 后 Oblivone 相对 quick 的优势到 3.6~5.3×（16K）。

## 2..128 × 叶子阈值 2..64 网格

```powershell
.\build\bench_grid.exe 1000
```

127 个长度 × 63 个阈值；每个 `(k, T, 分布)` 用 1000 组，同一 `key()`。
每个分布 6048 个 `(k, T)` 组合。

胜者区间（每个 k 取使 Oblivone 最快的 T）：

| 分布 | 插入占优 | Oblivone 稳定占优 | quick |
|---|---|---|---|
| 均匀 | 2..47（34..47 有零星 Oblivone） | 48..128 | 无稳定区间 |
| 正态 | 2..46（35..46 有零星） | 47..128 | 无稳定区间 |
| 指数 | 2..11 | 12..128 | 无稳定区间 |
| 少值 | 2..34（29 一次） | 35..128 | 无稳定区间 |

行数统计（每个分布 6048 个 `(k, T)`）：

| 分布 | 插入 | quick | Oblivone |
|---|---:|---:|---:|
| 均匀 | 1455 | 93 | 4500 |
| 正态 | 1537 | 57 | 4454 |
| 指数 | 288 | 78 | 5682 |
| 少值 | 858 | 6 | 5184 |

- **quick 没有任何稳定区间**，只在少数 `(k, T)` 行靠噪声/极小 T 赢；
- 稳定交叉点：指数 **12**、少值 **35**、正态 **47**、均匀 **48**；
- k≥48 时最佳 T 的中位数：均匀 25、正态 20、指数 34、少值 9
- 当前取 `leaf_sort=22`，落在中位数 20~34 附近，是合理折中；

## 加权期望（默认 1:1:1:1）

```powershell
py -3 bench/weighted.py build/grid_2_128.csv
# 或指定权重：uniform normal exponential few
py -3 bench/weighted.py build/grid_2_128.csv 0.4 0.3 0.2 0.1
```

没有可靠的“真实分布概率”，按 1:1:1:1：

- k=2..32 插入占优，**k=33..128 Oblivone 占优**；
- 6048 个 (k,T) 行：插入 824、quick 79、Oblivone 5145；
- k=16..128 平均：插入 2269、quick 2248、Oblivone **1214 ns/组**；
  相对 quick **1.85×**，相对插入 **1.87×**；
- 当前取 **22**：1:1 加权的中位最优；生产配置 `small_sort=16`、`leaf_sort=22`
  的期望约 1268，T=24 是 1266，差 <0.2%；
- 关键长度（插入/quick/Oblivone 最佳）：
  k=33：607/837/585（T=24）；
  k=48：1127/1408/870（T=23）；
  k=128：5372/4444/2076。

权重敏感性（只改分布权重，长度仍均匀）：

| 权重 | 交叉点 | 平均加速 vs quick |
|---|---:|---:|
| 1:1:1:1 | 33 | 1.85× |
| 均匀偏重 4:3:2:1 | 33 | 1.81× |
| 正态偏重 2:5:2:1 | 35 | 1.81× |
| 指数偏重 2:2:5:1 | 24 | 2.05× |
| 少值偏重 2:2:2:4 | 35 | 1.82× |

结论：交叉点在 24~35 之间，**指数分布最偏向 Oblivone**；
- 当前 `leaf_sort=22` 是 1:1 加权中位最优；T=24 差 <0.2%，T=32 差 <1%。

## 小 n：2 → 64K 扫描

```powershell
.\build\bench_small.exe 1000000
```

尺寸：2..16 步长 1；32..128 步长 16；256..1024 步长 128；
2048..4096 步长 1024；8192..16384 步长 4096；32768..65536 步长 16384。
统一用 `key()` 比较，单位 ns/array（每块独立排序，总量 1e6）：

| n | 插入 | `std::sort` | Oblivone+插入 | 纯 Oblivone |
|---:|---:|---:|---:|---:|
| 8 | **73.6** | 99.8 | 142.6 | 298.9 |
| 16 | **208.0** | 278.0 | 302.5 | 601.6 |
| 32 | **572.7** | 806.1 | 722.6 | 1151.3 |
| 48 | **1075.9** | 1412.7 | 1089.2 | 1712.4 |
| 64 | 1704.1 | 1991.9 | **1341.9** | 2321.9 |
| 128 | 5687.6 | 4656.8 | **2529.9** | 4527.1 |
| 1024 | 277896.6 | 51408.5 | **17316.7** | 30838.9 |
| 65536 | — | 5053746.7 | **1226253.3** | 2056646.7 |

- 插入排序在 n≤48 仍最快；**n≈64 起 Oblivone 胜出**，n≥256 差距明显；
- **纯 Oblivone 全程未胜出**：n=64 时 2322 vs 1342，n<16 时慢 3~5 倍；
- 插入排序 n>1024 未测（O(n²)），标 `-1`；
- 16~64 这段两者接近，交叉点受 best-of / median 影响，不要当成精确阈值；
  明确的是：**纯 Oblivone 不可取，叶子必须插入**。

## 三档探针

`n ≤ 1,000,000` 时，基准会自动追加三组探针：

- `block k=...`：块内随机、块间有序时，**全量插入 vs 逐块插入**；
- `k=...`：**插入 / 快排 / Oblivone 核心**在同一套 `key` 比较下的交叉点；
- `small_sort=16/64`：同一批分布下默认阈值的对比。

结果同步在 `README.md` 的“三档阈值”和 `docs/design.md` §7.1。

`bench_sort` 还会输出一行 `leaf T=...`：令 `small_sort = leaf_sort = T`，
扫 T=4/8/12/16/24/32/48/64 的端到端时间，用来定叶子阈值。

## 注意

这是“冒烟/回归”基准，不是发表级基准：取 3 次最好值、不绑核、不做统计检验。
README 里的数字来自 `n = 1,000,000`、`max_buckets = 65,536`、
MinGW g++ 15.2 `-O2`、i5-13500H。

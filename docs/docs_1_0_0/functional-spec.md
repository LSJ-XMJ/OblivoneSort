# 功能属性说明

## 1. 定位

OblivoneSort 是一个 C++17、header-only、非稳定的**数值键分布排序**。

核心流程：

```text
值 -> 保序 uint64_t key
   -> 按 key 直接计算桶号
   -> 计数
   -> 前缀和
   -> 散播到输出缓冲
   -> 桶内递归 / 插入 / fallback
```

它不是比较排序；它利用 key 的数值结构直接定位，因此平均复杂度可以低于比较排序的 Ω(n log n)。

## 2. 公共 API

### 2.1 单线程

```cpp
#include <oblivone/sort.hpp>

template <class T, class Fallback = default_fallback>
void sort(T* data, std::size_t n,
          const options& opt = {}, Fallback fallback = Fallback{});

template <class T, class Fallback = default_fallback>
void sort(std::vector<T>& values,
          const options& opt = {}, Fallback fallback = Fallback{});

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(T* data, std::size_t n, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{});

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(std::vector<T>& values, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{});
```

别名：

```cpp
oblivone::xsort
oblivone::xsort_by
```

`sort` 对算术类型使用默认 `key_of`。  
`sort_by` 使用用户 key，并对 key 的返回值做 `uint64_t` 转换。

### 2.2 并行

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_sort(...);
oblivone::parallel_sort_by(...);
oblivone::parallel_options;
oblivone::thread_pool;
```

核心头不依赖 `<thread>`、`<mutex>`。  
不 include `parallel.hpp` 时，不会链接线程设施。

## 3. 支持的类型

默认支持：

- 有符号整数；
- 无符号整数；
- `float`；
- `double`。

其他类型必须使用 `sort_by` 提供 key。

key 必须满足：

```text
非 NaN 且 x < y  =>  key(x) < key(y)
x == y           =>  key(x) == key(y)
```

## 4. key 语义

### 4.1 整数

- 无符号：按值映射；
- 有符号：符号扩展到 64 位后翻转符号位。

### 4.2 浮点

- 所有 NaN 映射到 `UINT64_MAX`；
- `-0.0` 归一化为 `+0.0`；
- 正数使用位模式加符号位；
- 负数使用位模式取反；
- 结果保序。

### 4.3 自定义 key

`sort_by`：

- key 返回可转换为 `uint64_t` 的值；
- 不启用 double 原生叶子优化；
- key 必须保序；
- key 抛异常时向调用者传播。

## 5. 参数

```cpp
struct options {
    std::size_t small_top      = 48;
    std::size_t small_sort     = 16;
    std::size_t leaf_sort      = 22;
    std::size_t scan_threshold = 256;
    int         max_depth      = 16;
    std::size_t bucket_limit   = 65536;
};
```

### 5.1 `small_top`

顶层 `n <= small_top` 直接交给 fallback。

默认 48。

### 5.2 `small_sort`

核心组 `m < small_sort` 交给 fallback。

默认 16。

### 5.3 `leaf_sort`

组 `m < leaf_sort` 使用插入排序。

默认 22。

### 5.4 `scan_threshold`

嵌套块 `m > scan_threshold` 时先做：

1. 升序扫描；
2. 降序扫描；
3. 预算插入。

默认 256。  
`0` 表示关闭嵌套扫描。

### 5.5 `max_depth`

递归深度上限，到顶后交给 fallback。

默认 16。

### 5.6 `bucket_limit`

每层桶数硬上限。

实际桶数：

```text
B = min(m, bucket_limit)
```

默认 65536。  
`0` 按 1 处理。

## 6. 默认 fallback

```cpp
struct default_fallback {
    template <class T, class Less>
    void operator()(T* first, T* last, Less less) const;
};
```

行为：

- 算术类型且长度 ≤96：插入排序；
- 其他：`std::sort`；
- 始终使用传入的 `less`。

用户 fallback 契约：

- 使用同一个 less；
- 最坏 O(k log k)；
- 可抛异常；
- 不改变元素数量。

## 7. 排序语义

- **不稳定**；
- 相等元素的相对顺序不保证；
- double 的 NaN 全部最后；
- `-0.0` 与 `+0.0` 视为相等；
- `n > UINT32_MAX` 直接走 fallback；
- 异常只提供基本保证：元素可能被移动，但不泄漏。

## 8. 单线程流程

```text
sort(data, n):
    n < 2 -> return
    n > UINT32_MAX -> fallback
    n <= small_top -> fallback
    已升序 -> return
    已降序 -> reverse
    预算插入成功 -> return
    分配 buf[n]
    core(data, n, depth=0)
```

预算：

```text
预算 = n / 8 次搬移
```

超预算时把当前元素放回数组，数组仍是一个置换，然后继续核心排序。

## 9. 核心流程

```text
core(a, m, depth):
    m < 2 -> return
    depth > 0 且 m > scan_threshold:
        有序扫描 / reverse / 预算插入
    m < small_sort 或 depth >= max_depth -> fallback
    min/max key
    min == max -> return
    B = min(m, bucket_limit)
    count[B]
    prefix -> start[B]
    scatter -> buf
    copy buf -> a
    每个组:
        len < 2 -> skip
        len < leaf_sort -> 插入 / 2/3 元素专用
        len < small_sort -> fallback
        else -> core(depth+1)
```

## 10. 并行

```cpp
struct parallel_options {
    unsigned threads = 0;
    std::size_t min_n = 2000000;
    std::size_t max_buckets = 65536;
    std::size_t scratch_budget = 0;
    thread_pool* pool = nullptr;
};
```

默认：

| 参数 | 默认 | 行为 |
|---|---:|---|
| `threads` | 0 | n<1e7 → min(hw,4)，n≥1e7 → min(hw,12) |
| `min_n` | 2,000,000 | 小于此值串行 |
| `max_buckets` | 65,536 | 每线程计数表上限 |
| `scratch_budget` | 0 | 0 = `n * sizeof(T)` |
| `pool` | nullptr | 空 = 临时线程池 |

并行阶段：

1. root count；
2. 合并计数、计算每线程每桶起点；
3. root scatter；
4. copy；
5. depth=1 组并行。

组内继续串行递归。  
`scratch_budget` 限制组并发内存。

## 11. 复杂度

### 时间

期望：

```text
O(n + B + Σ T(group))
```

分布匹配时通常 1~2 层，近似 O(n)。  
最坏由 `max_depth` 和 fallback 截住，为 O(n log n)。

### 空间

- `buf n`；
- 每层计数表；
- 并行 `T × B` 计数表；
- 组 scratch。

计数表使用 `uint32_t`，因此 `n > UINT32_MAX` 必须 fallback。

## 12. 适用与不适用

适合：

- 随机浮点/整数；
- 平滑分布；
- 大 n；
- 允许 O(n) 额外空间；
- 可接受不稳定排序。

不适合：

- 已排序/近有序为主；
- 大量长 run、organ pipe、push；
- 稳定排序；
- 字符串/结构体无 key；
- 内存严格受限；
- n 很小且直接 `std::sort` 更简单。

## 13. 已知结构性限制

1. scatter 会按值域切断原始 run；
2. 嵌套扫描发生在 scatter 之后，救不回跨桶 run；
3. 重尾/离群点会增大桶内元素，多递归一层；
4. key 转换和随机写是主要成本；
5. 单机基准不能代表所有硬件。

## 14. 第三方依赖

运行库不使用第三方库。

- 单线程：C++17 标准库；
- 并行：`std::thread` / `std::mutex` / `std::condition_variable`；
- CMake 链接系统 Threads；
- 不使用 Boost、TBB、OpenMP、pdqsort、ska_sort、TimSort 等。

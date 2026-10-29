# 2.0.0 测试记录

## 1. 编译环境

```text
GCC       MinGW GCC 15.2, -std=c++20, -O2
MSVC      Visual Studio 2022 19.39, Release
Boost     1.89（仅测试）
TimSort   cpp-TimSort（仅测试）
```

OblivoneSort 目标标准为 C++17；测试程序因为 cpp-TimSort 使用 C++20。

---

## 2. 测试数据

13 种模式：

```text
random
sorted
reverse
organ
sawtooth
runs
equal
few
near_0.1%
near_1%
near_5%
push_front
push_back
```

生成：

```text
double
均匀随机基础数据，固定 seed
sorted/reverse/equal 等由对应变换生成
```

---

## 3. 规模与线程

| n | 线程 | 用途 |
|---:|---:|---|
| 1,000,000 | 4 | 小规模并行 |
| 10,000,000 | 12 | 主规模并行 |

串行测试线程数为 1。

---

## 4. 对比算法

### 串行

```text
OblivoneSort sort
std::sort
std::stable_sort
Boost pdqsort
Boost spreadsort
cpp-TimSort gfx::timsort
```

### 并行

```text
OblivoneSort simple
OblivoneSort advanced
Boost block_indirect_sort
Boost sample_sort
Boost parallel_stable_sort
```

Boost/TimSort 只是测试依赖，不是运行时依赖。

---

## 5. 方法

```text
每个组合 3 次
取中位数
算法执行顺序按 pattern/rep 轮转
每次执行后验证 std::is_sorted(key)
```

轮转用于降低固定顺序带来的温度和 cache 偏差。

---

## 6. 数据文件

```text
test-data/gcc.csv
test-data/msvc.csv
```

列：

```text
scope,algorithm,pattern,n,threads,ms
```

每个组合只保留中位数。

---

## 7. 覆盖矩阵

```text
2 编译器
2 规模
13 模式
6 串行算法
5 并行算法
```

总记录：

```text
GCC   286 行
MSVC  286 行
```

---

## 8. 结果入口

摘要见：

[benchmarks.md](benchmarks.md)

原始结果：

- [GCC](test-data/gcc.csv)
- [MSVC](test-data/msvc.csv)

---

## 9. 正确性

所有测试执行后检查：

```text
有序
NaN 最后
multiset 等价
```

单元测试另覆盖：

```text
sort_by
cache
NaN 并行
custom key 并行
fuzz
```

---

## 10. 说明

```text
性能数据只对应当前机器和当前编译器
不同 CPU、线程数、Boost 版本会改变绝对值
强势算法对比不等于所有场景结论
```

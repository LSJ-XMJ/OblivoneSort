# 2.0.0 强势算法对比

## 1. 条件

```text
n = 10,000,000
串行：1 线程
并行：12 线程
13 种模式
每组合 3 次，中位数
算法顺序轮转
```

完整 1e6 / 1e7 原始数据：

- [GCC](test-data/gcc.csv)
- [MSVC](test-data/msvc.csv)

测试方法见 [test-record.md](test-record.md)。

---

## 2. GCC 15.2

单位 ms。`best/O` 表示对手最快值除以 Oblivone 值：

```text
> 1  Oblivone 更快
< 1  对手更快
```

| 模式 | 串行 O | 串行最强 | 串行 best/O | simple | advanced | 并行最强 | 并行 best/O |
|---|---:|---|---:|---:|---:|---|---:|
| random | 234.8 | spreadsort 447.2 | 1.90 | 54.2 | 58.5 | block 122.6 | 2.26 |
| sorted | 16.4 | spreadsort 8.6 | 0.53 | 15.8 | 16.8 | block 13.8 | 0.87 |
| reverse | 18.1 | timsort 19.8 | 1.09 | 16.7 | 20.0 | stable 20.1 | 1.21 |
| organ | 212.3 | timsort 38.8 | 0.18 | 63.3 | 66.3 | sample 29.5 | 0.47 |
| sawtooth | 190.2 | timsort 172.0 | 0.90 | 40.3 | 43.7 | block 46.8 | 1.16 |
| runs | 232.3 | spreadsort 455.7 | 1.96 | 52.6 | 55.5 | sample 98.9 | 1.88 |
| equal | 14.7 | spreadsort 8.7 | 0.59 | 15.2 | 15.2 | sample 14.0 | 0.92 |
| few | 97.1 | pdqsort 72.6 | 0.75 | 45.1 | 48.3 | block 30.9 | 0.69 |
| near 0.1% | 190.5 | timsort 122.4 | 0.64 | 49.7 | 49.0 | sample 48.7 | 0.99 |
| near 1% | 190.4 | timsort 161.6 | 0.85 | 47.8 | 49.4 | sample 52.0 | 1.09 |
| near 5% | 195.6 | spreadsort 239.9 | 1.23 | 50.1 | 50.2 | sample 59.5 | 1.19 |
| push front | 101.7 | timsort 19.7 | 0.19 | 39.9 | 41.0 | sample 19.0 | 0.48 |
| push back | 127.2 | timsort 19.8 | 0.16 | 64.9 | 64.6 | sample 32.5 | 0.50 |

---

## 3. MSVC 19.39

单位 ms：

| 模式 | 串行 O | 串行最强 | 串行 best/O | simple | advanced | 并行最强 | 并行 best/O |
|---|---:|---|---:|---:|---:|---|---:|
| random | 414.5 | spreadsort 487.1 | 1.18 | 65.9 | 70.4 | block 134.9 | 2.05 |
| sorted | 38.3 | spreadsort 9.2 | 0.24 | 28.7 | 27.6 | block 19.2 | 0.70 |
| reverse | 44.1 | timsort 26.8 | 0.61 | 36.0 | 35.4 | block 23.0 | 0.65 |
| organ | 270.2 | timsort 51.4 | 0.19 | 85.9 | 86.5 | sample 33.5 | 0.39 |
| sawtooth | 270.8 | timsort 195.2 | 0.72 | 46.5 | 48.6 | block 52.7 | 1.13 |
| runs | 278.5 | spreadsort 503.5 | 1.81 | 85.8 | 85.9 | block 134.4 | 1.57 |
| equal | 58.2 | spreadsort 11.8 | 0.20 | 37.4 | 41.5 | sample 23.8 | 0.64 |
| few | 253.6 | spreadsort 141.8 | 0.56 | 74.9 | 93.1 | block 39.4 | 0.53 |
| near 0.1% | 407.9 | timsort 152.2 | 0.37 | 103.5 | 113.8 | sample 74.3 | 0.72 |
| near 1% | 312.1 | timsort 248.6 | 0.80 | 95.1 | 93.3 | sample 84.7 | 0.91 |
| near 5% | 453.0 | spreadsort 359.9 | 0.79 | 92.4 | 113.4 | sample 92.7 | 1.00 |
| push front | 199.1 | timsort 33.8 | 0.17 | 70.9 | 76.1 | sample 29.3 | 0.41 |
| push back | 291.7 | timsort 36.4 | 0.12 | 138.2 | 122.9 | sample 62.7 | 0.45 |

---

## 4. 模式结论

GCC：

```text
领先：random, runs, sawtooth, near 1%, near 5%
接近：near 0.1%
落后：organ, push, few, sorted, equal
```

MSVC：

```text
领先：random, runs, sawtooth, near 5%（并行基本持平）
落后：organ, push, few, near 1%, near 0.1%
```

共同落后：

```text
organ        TimSort / Boost sample_sort 明显更快
push front   TimSort / Boost sample_sort 明显更快
push back    TimSort / Boost sample_sort 明显更快
sorted       spreadsort / Boost stable 更快
equal        spreadsort / Boost stable 更快
few          pdqsort / Boost block 更快
```

---

## 5. 结论

```text
分布排序在 random / smooth 数据上有优势
结构化 runs / organ / push 不是该算法强项
Boost sample_sort 在结构化数据上很强
TimSort 在 organ / push 上很强
```

因此不能宣称 2.0.0 全场景最快。

---

## 6. 1e6 数据

1e6 / 4 线程全部结果保存在：

- [gcc.csv](test-data/gcc.csv)
- [msvc.csv](test-data/msvc.csv)

可用列筛选：

```text
n == 1000000
threads == 4
```

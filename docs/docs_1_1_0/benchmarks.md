# 强势排序算法对比

## 1. 目的

说明 OblivoneSort 1.1.0 在常见排序算法中的位置，避免把“快于 `std::sort`”误解为“全场景最快”。

## 2. 对比算法

串行：

- `std::sort`
- `std::stable_sort`
- Boost 1.89 `pdqsort`
- Boost 1.89 `spreadsort`
- TimSort（cpp-TimSort 的 `gfx::timsort`）

并行：

- Boost 1.89 `sample_sort`
- Boost 1.89 `parallel_stable_sort`
- Boost 1.89 `block_indirect_sort`
- OblivoneSort `parallel_sort`

## 3. 测试条件

```text
GCC 15.2 / MSVC 19.39
13 种模式
1e6：4 线程
1e7：12 线程
每轮打乱算法顺序
所有模式验证有序
```

Boost/TimSort 只用于测试，不是 OblivoneSort 的运行时依赖。

## 4. 1e7 结果摘要

GCC，单位 ms；取每个模式最快实现：

| 模式 | 最快实现 | 耗时 | Oblivone 单线 | Oblivone 多线 |
|---|---|---:|---:|---:|
| random | Boost block_indirect | 63.8 | 232.5 | 90.1 |
| sorted | Boost sample_sort | 6.4 | 15.9 | 15.9 |
| reverse | TimSort | 13.0 | 17.4 | 17.3 |
| organ | Boost block_indirect | 23.6 | 200.7 | 97.7 |
| sawtooth | Boost block_indirect | 30.2 | 173.0 | 78.0 |
| runs | Boost block_indirect | 54.1 | 212.7 | 83.9 |
| equal | Boost block_indirect | 7.1 | 18.1 | 17.3 |
| few | Boost block_indirect | 24.1 | 94.1 | 64.6 |
| near 0.1% | Boost sample_sort | 41.8 | 183.9 | 86.3 |
| near 1% | Boost block_indirect | 42.7 | 188.9 | 74.6 |
| near 5% | Boost block_indirect | 43.8 | 191.5 | 77.0 |
| push front | TimSort | 13.3 | 109.9 | 57.2 |
| push back | TimSort | 15.1 | 131.8 | 88.0 |

MSVC 结论相同：Boost 并行 block/sample sort 在多数模式领先；TimSort 在 organ/push 最强。

## 5. 结论

- 随机/平滑，串行：pdqsort 与 OblivoneSort 接近；
- 随机/平滑，并行：Boost `block_indirect_sort` 通常最快；
- organ/equal/push：TimSort 最强；
- few/near-sorted：pdqsort 或并行 block sort；
- OblivoneSort 的优势是：
  - header-only；
  - 无第三方运行时依赖；
  - 分布排序在随机数据上简单直接；
- 如果允许 Boost/TBB，必须实际基准，不能假定 Oblivone 更快。

## 6. 缓存

`cache_bucket_index` 是 opt-in：

- 默认关闭；
- 只支持 `B <= 65536`；
- 约 `2n` 字节额外内存；
- 昂贵自定义 key 上收益更明确；
- 默认 key 和强并行算法下不保证收益。

缓存不改变上述算法定位，也不把 OblivoneSort 变成全场景最快。

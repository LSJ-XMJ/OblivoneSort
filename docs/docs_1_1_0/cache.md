# 桶号缓存

## 1. 用法

```cpp
#include <oblivone/sort.hpp>

oblivone::options opt;
opt.cache_bucket_index = true;

oblivone::sort(values, opt);
oblivone::sort_by(values, expensive_key, opt);
```

并行版本使用同一个 `options`：

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 8;

oblivone::parallel_sort(values, opt, popt);
oblivone::parallel_sort_by(values, expensive_key, opt, popt);
```

默认值：

```cpp
bool cache_bucket_index = false;
```

## 2. 生效条件

缓存仅在有效桶数满足：

```text
B = max(1, min(m, bucket_limit))
B <= 65536
```

时生效，使用 `uint16_t` 桶号表。

否则自动忽略缓存，继续使用普通路径：

- `B > 65536`；
- `bucket_limit` 被设为极大值，导致 `B = m`；
- 并行 `max_buckets=0` 且 `B > 65536`。

不会抛异常，也不会改变排序结果。

## 3. 实现方式

普通路径每个元素在 count 和 scatter 阶段各计算一次：

```text
key(value)
bucket_of(key)
```

缓存路径在 count 阶段保存桶号：

```text
count:   key(value) -> bucket -> index[i]
scatter: index[i] -> buf
```

从而在 scatter 阶段省掉一次 `key + bucket`。

单线程递归共用一张 `n` 大小的表：

- 父层 scatter 完成后，父层桶号不再需要；
- 子层可以覆盖表的前 `m` 个位置。

并行版本只缓存 root：

- root count 写入共享桶号表；
- root scatter 读取；
- 组递归仍然重算，不使用共享表。

## 4. 内存

额外内存约：

```text
2n 字节
```

实测峰值：

```text
1e7 baseline   158.8 MB
1e7 cached     177.9 MB  (+12%)
1e8 baseline   1532 MB
1e8 cached     1723 MB  (+12.5%)
```

## 5. 性能

缓存不是普遍加速。

| 场景 | 结果 |
|---|---|
| 单线程默认 key | 依赖编译器和类型，可能回退 |
| 单线程昂贵自定义 key | 13~26% 提升 |
| 并行默认 key | 可能回退 |
| 并行昂贵 key | 5~11% 提升 |
| 1e8 | 收益消失，只增加内存 |

建议：

- 优先用于昂贵 `sort_by` key；
- 默认整数 key 先基准再开启；
- 并行默认 key 不建议开启；
- 1e8 不建议开启；
- 不要设置 `bucket_limit` 为极大值。

## 6. 语义

- 排序仍不稳定；
- NaN 最后、`-0 == +0` 不变；
- 自定义 key、fallback、线程池行为不变；
- `cache_bucket_index` 只影响性能与内存，不影响结果。

# 实现说明

本文描述当前 1.0.0 代码的模块结构、数据流和关键实现约束。

## 1. 文件结构

```text
include/oblivone/
  sort.hpp                 # 单线程公共入口
  parallel.hpp             # 可选并行
  options.hpp              # 参数
  version.hpp              # 版本
  detail/
    key.hpp                # 保序 key、double/float 比较
    fallback.hpp           # 默认 fallback、插入排序
    core.hpp               # 分桶核心
    dispatch.hpp           # 顶层检查、调度
```

## 2. 单线程数据流

```text
sort
  -> prepare
       n < 2 / n > UINT32_MAX / n <= small_top
       升序扫描
       降序 reverse
       预算插入
  -> 分配 vector<T> buf(n)
  -> sort_range(depth=0)
       嵌套扫描
       终止判断
       min/max key
       count
       prefix
       scatter
       copy
       组处理
```

## 3. key 实现

`detail::key_of`：

- 无符号整数：直接转 `uint64_t`；
- 有符号整数：先 `int64_t` 符号扩展，再翻转最高位；
- `bool`：0/1；
- `float` / `double`：
  - NaN → `UINT64_MAX`；
  - `-0` → `+0`；
  - 正数：位模式加符号位；
  - 负数：位模式取反。

`less_double` / `less_float`：

```text
NaN 最后；其余按原生 <
```

`sort_by` 不启用上述原生比较，只使用用户 key。

## 4. 调度层

`detail::prepare` 返回：

- `false`：已经完成（有序、反向、预算插入完成、或已 fallback）；
- `true`：需要进入核心。

顺序：

1. `n < 2`；
2. `n > UINT32_MAX`；
3. `n <= small_top`；
4. 升序/降序扫描；
5. 预算插入；
6. 返回 true。

预算插入：

- 预算 `n/8` 次搬移；
- 超预算时把当前元素放回，保证数组仍是置换；
- 然后继续核心。

## 5. 核心实现

### 5.1 终止

```text
m < 2 -> return
m < small_sort -> fallback
depth >= max_depth -> fallback
```

### 5.2 嵌套扫描

```text
depth > 0
scan_threshold > 0
m > scan_threshold
```

执行：

1. 升序扫描；
2. 降序扫描；
3. 预算插入 `m/8`；
4. 未完成则继续分桶。

### 5.3 分桶

```text
B = min(m, max(1, bucket_limit))
scale = B / (max_key - min_key)
bucket(k):
    k <= min -> 0
    k >= max -> B - 1
    floor((k - min) * scale)
```

要求：

- 桶号单调；
- 不越界；
- 浮点精度只影响分布，不影响正确性。

### 5.4 计数与散播

```text
count[B] = 0
for x: count[bucket(key(x))]++

start[0] = 0
for b: start[b] = start[b-1] + count[b-1]
end[b] = start[b] + count[b]

pos = start
for x:
    b = bucket(key(x))
    buf[pos[b]++] = x

copy(buf, a)
```

关键不变量：

- `count` 总和 = m；
- `start`/`end` 覆盖整个段；
- 散播是置换；
- 桶间有序。

### 5.5 组处理

```text
len < 2 -> skip
len < leaf_sort -> 插入 / 2、3 元素专用
len < small_sort -> fallback
else -> sort_range(depth+1)
```

默认 16 < 22，所以核心 fallback 分支通常不可达。

## 6. 内存

- 顶层分配 `buf[n]`；
- 每个递归节点分配 `count`、`start`、`end`、`pos`；
- `buf` 递归共用；
- 计数使用 `uint32_t`；
- 因此 `n > UINT32_MAX` 必须先 fallback。

## 7. 并行实现

### 7.1 线程池

`thread_pool`：

- 固定线程数；
- `run(tasks, fn)` 提交并等待；
- 任务异常被捕获后在 `run()` 重抛；
- 同一个池的 `run()` 串行化；
- 不同池可并发。

### 7.2 root 并行的前提

并行路径同样调用 `detail::prepare`，所以：

- 有序/反向/近有序快速路径与单线程一致；
- 不会在已排序数据上创建线程。

### 7.3 分片

```text
begin = n * t / T
end   = n * (t + 1) / T
```

每个线程：

1. 局部 count；
2. 主线程合并；
3. 计算每线程每桶起点；
4. 并行 scatter；
5. 并行 copy。

### 7.4 组并行

```text
atomic next = 0
worker:
    scratch(max_group)
    b = next++
    if b >= B: break
    process_group(b)
```

`max_group` 是 root 最大组。

### 7.5 scratch 预算

```text
budget = scratch_budget ? scratch_budget : n * sizeof(T)
group_threads = min(T, budget / (max_group * sizeof(T)))
```

- 预算不足时降低组并发；
- 结果为 1 时串行处理组。

## 8. 关键边界

| 情况 | 行为 |
|---|---|
| n=0/1 | 直接返回 |
| 全相等 | `min_key == max_key` 返回 |
| NaN | key 最大，排在最后 |
| -0 | 与 +0 同 key |
| n > UINT32_MAX | fallback |
| 全部落一个桶 | 递归到 max_depth 后 fallback |
| 用户 key 不保序 | 行为未定义 |
| 用户 fallback 不满足 O(k log k) | 最坏界失效 |

## 9. 性能相关实现选择

- 小段用插入，避免分桶表开销；
- 顶层和嵌套都先尝试有序/近有序；
- `bucket_limit` 默认 64K，控制计数表缓存；
- 并行 root 表按线程私有，避免原子计数；
- 组并行只做 depth=1，避免过度并行；
- scratch 预算控制大组内存。

## 10. 非目标

当前实现不包含：

- 稳定排序；
- run-aware / TimSort 路由；
- 原地置换；
- 自定义 allocator；
- SIMD；
- GPU；
- 第三方排序库。

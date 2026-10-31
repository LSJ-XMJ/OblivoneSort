# 2.0.1 并行设计

## 1. 结构

```text
simple   thread_pool + phase barrier + group tasks
advanced engine + pump + observer + reservation
```

两个后端共享：

```text
串行 sort_range
值域粗桶
局部 bucket_limit
```

执行层独立。

---

## 2. 两阶段分布

### 2.1 root 粗桶

```text
P = coarse_buckets
  或 clamp(n / 8000, 512, 1024)
```

只分 P 个粗桶，不直接分 65536。

目标：

```text
降低 count / prefix / scatter 表大小
提高 cache locality
使粗桶内部可以并行
```

### 2.2 粗桶内部

每个粗桶调用 `detail::sort_range`：

```text
local_bucket_limit =
  local_bucket_limit
  或 n <= 3M ? 1024 : n <= 15M ? 2048 : 4096
```

粗桶内仍使用串行分布排序。

---

## 3. 为什么使用 value 域

公共 key 保序但非线性：

```text
key 空间等宽 != value 空间等宽
```

double 上按 key 等宽会产生数百万元素的大桶，导致：

```text
单桶串行
线程利用率下降
```

所以算术类型使用：

```text
(value - min) / (max - min) * P
```

### 回退条件

```text
非算术 T
含 NaN
value range 无效
```

自动回退旧的 key 分桶。

---

## 4. simple 并行

阶段：

```text
parallel min/max
coarse count
coarse prefix / offsets
coarse scatter
copy
coarse block sort
```

线程模型：

```text
thread_pool
indexed task
phase barrier
```

每个粗桶排序：

```text
scratch per worker
sort_range(group, local_bucket_limit)
```

特性：

```text
固定线程
无 work stealing
实现直接
适合作为正式 API
```

---

## 5. advanced 实验后端

### engine

```text
每 worker 双端队列
outer / inner / group lanes
work stealing
reservation
```

### root_node

```text
并行 min/max
task based count
task based scatter / copy
pump group plans
```

### pump / observer

```text
worker 空闲时观察
动态提高 team size
group 任务进入 allocator
```

### child partition

大组可以递归创建 child node。

预算：

```text
max_nodes
memory_budget
scratch_budget
```

### executor

```text
复用 engine
一个调用线程
异常后重建
```

advanced 不进入安装包。

---

## 6. 内存

simple：

```text
n 元素 buffer
T x P count / offsets
largest group scratch per worker
```

advanced：

```text
node count / offset / buffer
pump scratch
node admission
```

P 从 65536 降到 512~1024 后，小表内存和 cache 压力下降。

---

## 7. 正确性

并行路径复用串行 key/比较器：

```text
min/max
count
scatter
sort_range
```

不改变：

```text
NaN 最后
-0 == +0
不稳定
自定义 key 语义
```

所有模式在测试中验证：

```text
有序
multiset 一致
```

---

## 8. 性能边界

优势：

```text
random
runs
sawtooth
near 1% / 5%
```

持平：

```text
near 0.1%
sorted
equal
```

落后：

```text
organ
push front/back
few
```

原因：

```text
Boost sample_sort / TimSort 对结构化 runs 更强
```

---

## 9. 参数

```cpp
coarse_buckets = 0;      // 0 = auto
local_bucket_limit = 0;  // 0 = auto
```

手动覆盖用于实验，不建议普通用户调整。

---

## 10. 限制

```text
不稳定
额外 n 空间
advanced 非安装 API
结构化数据不保证领先
```

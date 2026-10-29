# 2.0.0 单线程设计

## 1. 目标

`oblivone::sort` / `sort_by` 是 header-only、原地外层、线性额外空间的分布排序。

```text
不稳定排序
额外 n 个元素空间
整数 / 浮点 / 自定义 key
```

---

## 2. 顺序模型

key 是保存顺序的 `uint64_t`：

```cpp
key(a) < key(b)  <=>  a < b
```

默认：

```text
整数：符号位翻转后按无符号比较
double/float：IEEE-754 位模式变换
```

性质：

```text
NaN 排在最后
-0 == +0
所有 NaN 等价
```

自定义 key 必须自己保证保序。

---

## 3. 主流程

```text
prepare
  -> 已有序 / 逆序直接返回

小数组
  -> insertion / network

min/max key

count
prefix
scatter

逐组递归

叶组
  -> sort2 / sort3 / budgeted insertion / fallback
```

---

## 4. prepare

顺序扫描：

```text
ascending 前缀
descending 前缀
```

如果整个数组已经升序或降序，直接返回。

这一步使 sorted / reverse / 大段 runs 的处理接近 O(n)，避免无意义分桶。

---

## 5. 小数组

阈值：

```text
n <= small_top   -> insertion
n <= leaf_sort   -> budgeted insertion
n == 2 / 3       -> 专用 network
```

`budgeted_insertion` 带比较预算，退化输入及时切到 fallback。

---

## 6. 分桶

```text
B = min(n, bucket_limit)
span = max_key - min_key
scale = B / span
```

映射：

```text
key <= min -> 0
key >= max -> B - 1
否则 floor((key - min) * scale)
```

越界钳制到 `B-1`。

---

## 7. count / prefix / scatter

```text
count[b]      每个桶元素数
start[b]      桶区间起点
end[b]        桶区间终点
pos[b]        scatter 写指针
```

scatter 后：

```text
data[start[b] .. finish[b])
```

是同一个桶，但桶内无序。

---

## 8. 组处理

对每个桶：

```text
len < small_sort -> fallback / insertion
len 大          -> 递归 sort_range
```

递归深度受 `max_depth` 限制。

递归分组仍然是：

```text
count
prefix
scatter
```

直到达到叶阈值。

---

## 9. fallback

当：

```text
递归过深
比较预算耗尽
用户提供 fallback
```

调用 fallback。

默认 fallback：

```text
introsort
```

自定义 key 可以注入自己的比较器。

---

## 10. NaN 与等值

NaN key 全部映射到最后桶，保持等价。

`-0` 与 `+0` 的 key 相同，顺序不保证。

---

## 11. 缓存

`cache_bucket_index`：

```text
仅 B <= 65536
额外约 2n 字节
保存每元素桶号，避免重复计算 key
```

默认关闭。

适合：

```text
昂贵自定义 key
```

默认 double 上收益不保证。

---

## 12. 复杂度

```text
时间   平均 O(n)，最坏受 fallback 限制
空间   n 元素 buffer + B count/prefix
```

`B` 默认 65536，但小 n 会被压到 n。

---

## 13. 边界

```text
不稳定
需要额外 n 空间
极小 n 串行
结构化数据不保证优于 TimSort
```

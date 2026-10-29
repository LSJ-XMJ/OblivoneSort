# OblivoneSort 2.0.0 Design

## 1. Scope

OblivoneSort is a C++17 header-only distribution sort.

```text
serial sort / sort_by
simple parallel_sort / parallel_sort_by
advanced experimental scheduler (not installed)
unstable
n extra elements
order-preserving uint64_t key
```

---

## 2. Ordering

```cpp
key(a) < key(b)  <=>  a < b
```

Default keys:

```text
integers: sign-bit transform, unsigned compare
double/float: IEEE-754 order-preserving transform
```

Properties:

```text
NaN sorts last
-0 == +0
all NaNs equivalent
```

Custom keys must preserve order.

---

## 3. Serial pipeline

```text
prepare:
  detect ascending / descending prefix

small input:
  insertion / sorting networks

min/max key
count
prefix
scatter
recursive groups
leaf insertion or fallback
```

---

## 4. Prepare

A single scan detects already sorted and reverse-sorted prefixes.

If the full range is ordered, return without partitioning.

This makes sorted / reverse / long runs close to O(n).

---

## 5. Buckets

```text
B = min(n, bucket_limit)
scale = B / (max_key - min_key)
bucket(key) = floor((key - min) * scale)
```

Clamp:

```text
key <= min  -> 0
key >= max  -> B - 1
```

`bucket_limit` defaults to 65536.

---

## 6. Count, prefix, scatter

```text
count[b]
start[b]
finish[b]
position[b]
```

After scatter, each bucket is a contiguous unordered range.

Groups are processed recursively until a leaf threshold.

---

## 7. Fallback

Fallback is used when:

```text
recursion depth exceeds max_depth
comparison budget is exhausted
user supplies a fallback
```

Default fallback is introsort.

---

## 8. Serial memory

```text
n-element buffer
B count / prefix / cursor tables
```

---

## 9. Parallel overview

Two backends share the value-aware coarse partition and the serial
`sort_range`, but use different schedulers.

```text
simple   thread_pool + phase barriers
advanced engine + pump + observer + reservation
```

---

## 10. Value-aware coarse partition

Root bucket count:

```text
P = coarse_buckets
   or clamp(n / 8000, 512, 1024)
```

Root partitions by numeric value:

```text
(value - min) / (max - min) * P
```

The public key transform is order-preserving but not value-linear. Key-width
partitions on doubles create very large buckets, so arithmetic inputs use the
value range.

Fallback to key partitioning when:

```text
T is not arithmetic
NaN is present
value range is invalid
```

Each coarse block is sorted with:

```text
local_bucket_limit =
  local_bucket_limit
   or n <= 3M ? 1024 : n <= 15M ? 2048 : 4096
```

---

## 11. simple parallel

Phases:

```text
parallel min/max
coarse count
coarse prefix / offsets
coarse scatter
copy
coarse block sort
```

Model:

```text
thread_pool
indexed tasks
phase barriers
scratch per worker
```

It is the installed parallel API.

---

## 12. advanced experimental

Engine:

```text
per-worker deques
outer / inner / group lanes
work stealing
reservation
```

Root node:

```text
parallel min/max
task-based count
task-based scatter / copy
pump group plans
```

Observer raises the participating team when workers become idle.

Large groups can create recursive child nodes.

Budgets:

```text
max_nodes
memory_budget
scratch_budget
```

`executor` reuses the engine. It is not installed.

---

## 13. Correctness

Both parallel backends reuse the serial:

```text
key
comparator
min/max
count
scatter
sort_range
```

Semantics remain:

```text
unstable
NaN last
-0 == +0
custom key ordering
```

---

## 14. Performance boundaries

Advantages:

```text
random
runs
sawtooth
near-sorted with perturbations
```

Weaknesses:

```text
organ
push front / back
few distinct values
```

Boost `sample_sort` and TimSort are stronger on structured runs.

---

## 15. Parameters

```cpp
coarse_buckets = 0;      // 0 = automatic
local_bucket_limit = 0;  // 0 = automatic
```

Manual overrides are intended for experiments.

# OblivoneSort 2.0.0 Parallel Design

## 1. Structure

```text
simple    thread_pool + phase barriers + group tasks
advanced  engine + pump + observer + reservation
```

Both backends share:

```text
serial sort_range
value-aware coarse partition
local bucket limit
```

Scheduling is independent.

---

## 2. Two-stage partition

### 2.1 Root coarse partition

```text
P = coarse_buckets
   or clamp(n / 8000, 512, 1024)
```

The root stage creates only P coarse buckets, not 65536.

Goals:

```text
smaller count / prefix tables
better cache locality
parallel sorting inside coarse blocks
```

### 2.2 Inside a coarse block

Each coarse block calls `detail::sort_range`:

```text
local_bucket_limit =
  local_bucket_limit
   or n <= 3M ? 1024 : n <= 15M ? 2048 : 4096
```

Distribution sorting inside a block remains serial.

---

## 3. Value-domain mapping

The public key is order-preserving but not value-linear:

```text
equal-width key buckets != equal-width value buckets
```

On doubles, key-width partitioning creates very large buckets and reduces
parallel utilization.

Arithmetic inputs use:

```text
(value - min) / (max - min) * P
```

Fallback to key partitioning when:

```text
T is not arithmetic
NaN is present
value range is invalid
```

---

## 4. simple backend

Phases:

```text
parallel min/max
coarse count
coarse prefix / offsets
coarse scatter
copy
coarse block sort
```

Thread model:

```text
thread_pool
indexed tasks
phase barriers
per-worker scratch
```

Each coarse block is sorted by:

```text
sort_range(group, local_bucket_limit)
```

Properties:

```text
fixed worker count
no work stealing
straightforward phase structure
installed parallel API
```

---

## 5. advanced backend

### Engine

```text
per-worker deques
outer / inner / group lanes
work stealing
reservation
```

### Root node

```text
parallel min/max
task-based count
task-based scatter / copy
pump group plans
```

### Pump and observer

```text
idle workers are observed
team size is raised dynamically
group plans enter the allocator
```

### Child partition

Large groups can create recursive child nodes.

Budgets:

```text
max_nodes
memory_budget
scratch_budget
```

### Executor

```text
reuses the engine
one caller thread at a time
recreate after an exception
```

The advanced backend is not part of the installed package.

---

## 6. Memory

simple:

```text
n-element buffer
T x P count / offset tables
per-worker scratch for the largest coarse block
```

advanced:

```text
node count / offset / buffer
pump scratch
node admission
```

Reducing the root bucket count from 65536 to 512~1024 lowers table memory and
cache pressure.

---

## 7. Correctness

Both backends reuse the serial:

```text
key
comparator
min/max
count
scatter
sort_range
```

Semantics are unchanged:

```text
unstable
NaN last
-0 == +0
custom key ordering
```

Every benchmark execution verifies:

```text
sorted order
multiset equivalence
```

---

## 8. Performance boundaries

Advantages:

```text
random
runs
sawtooth
near 1% / 5%
```

Close:

```text
near 0.1%
sorted
equal
```

Behind:

```text
organ
push front / back
few distinct values
```

Boost `sample_sort` and TimSort are stronger on structured runs.

---

## 9. Parameters

```cpp
coarse_buckets = 0;      // 0 = automatic
local_bucket_limit = 0;  // 0 = automatic
```

Manual overrides are experimental.

---

## 10. Limitations

```text
unstable
n extra elements required
advanced backend has no stable API
structured data is not a guaranteed win
```

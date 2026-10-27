# OblivoneSort 1.0.0 — Code Design

## 1. Overview

OblivoneSort is a C++17, header-only, unstable distribution sort.

The implementation maps each value to an order-preserving `uint64_t` key, computes a bucket index from that key, counts bucket sizes, builds prefix offsets, scatters elements into an output buffer, and sorts each bucket recursively or with insertion sort.

It is not a comparison sort. The design relies on the numeric structure of the key, so it can avoid the comparison lower bound on suitable distributions.

## 2. Goals

- Header-only, C++17.
- No third-party runtime dependency.
- Support arithmetic keys and custom key functions.
- Stable public API and default parameters.
- Optional parallel header without making the core depend on threads.
- Bounded worst case through fallback.
- Clear NaN / negative-zero behavior for floating point.

## 3. Non-goals

- Stable sorting.
- Run-aware or TimSort-style structured-data optimization.
- In-place operation.
- Custom allocators.
- SIMD or GPU acceleration.
- Third-party sorting implementations.
- String or arbitrary object support without a user-provided key.

## 4. File Structure

```text
include/oblivone/
  sort.hpp                 public single-thread API
  parallel.hpp             optional parallel API
  options.hpp              public parameters
  version.hpp              version string
  detail/
    key.hpp                order-preserving key mapping
    fallback.hpp           default fallback and insertion sort
    core.hpp               bucket core
    dispatch.hpp           prechecks and dispatch
```

Dependency direction:

```text
key
  -> fallback
  -> core
  -> dispatch
  -> sort
  -> parallel
```

`parallel.hpp` is the only public header that includes thread facilities.

## 5. Public API Boundary

`sort.hpp` exposes:

```cpp
oblivone::sort
oblivone::sort_by
oblivone::xsort
oblivone::xsort_by
oblivone::key_of
oblivone::options
oblivone::default_fallback
```

`parallel.hpp` exposes:

```cpp
oblivone::parallel_sort
oblivone::parallel_sort_by
oblivone::parallel_options
oblivone::thread_pool
```

Internal helpers live under `oblivone::detail`.

## 6. Key Mapping

### 6.1 Contract

For non-NaN values:

```text
x < y  =>  key(x) < key(y)
x == y =>  key(x) == key(y)
```

Key type is always `uint64_t`.

### 6.2 Integers

- Unsigned integers are widened to `uint64_t`.
- Signed integers are sign-extended to 64 bits, then the sign bit is flipped.

### 6.3 Floating Point

- Every NaN maps to `UINT64_MAX`.
- `-0.0` is normalized to `+0.0`.
- Positive values use the bit pattern with the sign bit set.
- Negative values use the inverted bit pattern.
- `float` is widened after mapping.

This gives a total order consistent with numeric order, with all NaNs at the end.

### 6.4 Custom Keys

`sort_by` accepts a user key function. The returned value is converted to `uint64_t`.

Custom keys:

- must be order-preserving;
- disable the native double leaf optimization;
- must be safe to call concurrently when used through the parallel API.

## 7. Dispatcher

The dispatcher performs cheap checks before entering the bucket core:

```text
if n < 2: return
if n > UINT32_MAX: fallback
if n <= small_top: fallback

if already ascending: return
if already descending: reverse and return
if budgeted insertion completes: return

allocate buf[n]
run core at depth 0
```

The budgeted insertion uses at most `n/8` moves. If the budget is exhausted, the current element is placed back so the array remains a permutation, then the core continues.

## 8. Core Algorithm

### 8.1 Entry Checks

```text
if m < 2: return
if depth > 0 and scan_threshold > 0 and m > scan_threshold:
    ordered scan / reverse / budgeted insertion
if m < small_sort or depth >= max_depth:
    fallback
```

### 8.2 Bucket Count

```text
min_key = minimum key in segment
max_key = maximum key in segment
if min_key == max_key: return

B = min(m, max(1, bucket_limit))
scale = B / (max_key - min_key)
```

Bucket index:

```text
if key <= min_key: 0
if key >= max_key: B - 1
otherwise: floor((key - min_key) * scale)
```

The clamp is required for floating-point rounding and for the maximum key.

### 8.3 Counting and Prefix Offsets

```text
count[B] = 0
for each element:
    count[bucket(key)]++

start[0] = 0
for b in 1..B-1:
    start[b] = start[b-1] + count[b-1]

end[b] = start[b] + count[b]
```

`count` uses `uint32_t`. Therefore `n > UINT32_MAX` must use fallback.

### 8.4 Scatter

```text
pos = start
for each element:
    b = bucket(key)
    buf[pos[b]++] = element

copy buf[0..m) back to the segment
```

The scatter is a permutation:

- every position is written once;
- no element is lost or duplicated;
- bucket intervals are disjoint;
- buckets appear in key order.

### 8.5 Group Processing

For each bucket interval `[start[b], end[b])`:

```text
len < 2          -> skip
len < leaf_sort  -> insertion / 2-3 element special case
len < small_sort -> fallback
otherwise        -> core(depth + 1)
```

Default `small_sort=16` and `leaf_sort=22`, so the middle fallback case is normally unreachable.

## 9. Fallback

The default fallback:

- uses insertion sort for arithmetic types up to 96 elements;
- uses `std::sort` otherwise;
- always uses the supplied comparator.

A user fallback must:

- use the same comparator;
- have O(k log k) worst-case behavior;
- preserve the number of elements;
- may throw.

The fallback is used for:

- top-level small arrays;
- `n > UINT32_MAX`;
- core segments below `small_sort`;
- recursive depth limit.

## 10. Correctness Invariants

1. Bucket index is monotone in the key.
2. Bucket intervals are contiguous and ordered.
3. `start` is the prefix sum of bucket counts.
4. Scatter writes each position exactly once.
5. Recursive buckets partition the segment without overlap.
6. Sorting each bucket and concatenating in bucket order yields a globally sorted segment.

Correctness does not depend on bucket balance. Poor balance only increases recursion depth.

## 11. Complexity

Expected:

```text
O(n + B + sum of group costs)
```

For smooth distributions, recursion depth is typically 1–2, giving near-linear behavior.

Worst case:

- all elements remain in one bucket;
- recursion reaches `max_depth`;
- fallback provides O(n log n).

Space:

- output buffer of `n` elements;
- count/start/end tables per active recursion level;
- parallel per-thread count tables and group scratch.

## 12. Parallel Design

### 12.1 Structure

```text
serial prechecks
parallel root count
merge local counts
compute per-thread per-bucket offsets
parallel scatter
parallel copy
parallel depth-1 groups
```

Group recursion remains serial.

### 12.2 Thread Pool

`thread_pool::run(tasks, fn)`:

- submits `tasks` jobs;
- waits for all jobs;
- captures the first exception and rethrows it;
- serializes calls to the same pool;
- allows different pools to run concurrently.

### 12.3 Memory Bounds

- `parallel_options::max_buckets` caps per-thread count tables.
- `scratch_budget` caps group scratch memory.
- Group concurrency is reduced when `scratch_budget` cannot hold `threads × max_group`.

### 12.4 Defaults

```text
threads        = 0 (auto)
min_n          = 2,000,000
max_buckets    = 65,536
scratch_budget = 0 (n * sizeof(T))
pool           = nullptr
```

Auto thread count:

```text
n < 1e7  -> min(hardware, 4)
n >= 1e7 -> min(hardware, 12)
```

## 13. Error and Edge Behavior

| Case | Behavior |
|---|---|
| empty / one element | return |
| all keys equal | return |
| NaN | key is maximum, sorted last |
| -0.0 | same key as +0.0 |
| ±Inf | mapped through bit pattern |
| `n > UINT32_MAX` | fallback |
| all elements in one bucket | recurse to `max_depth`, then fallback |
| comparator/key throws | exception propagates; basic guarantee |
| invalid custom key order | undefined behavior |

## 14. Concurrency

- Core single-thread API has no global state and is reentrant.
- Different parallel sorts may run concurrently with different pools.
- A single pool serializes its `run()` calls.
- User key, comparator, and fallback must be thread-safe when using the parallel API.
- A user-provided pool must outlive the sort call.

## 15. Performance-Sensitive Choices

- Small groups use insertion sort to avoid table overhead.
- Top-level and nested ordered scans are cheap early exits.
- `bucket_limit=65,536` keeps count tables cache-friendly and bounds parallel table memory.
- Root parallel tables are thread-private to avoid atomic counting.
- Only depth-1 groups are parallelized to avoid oversubscription.
- Scratch budgeting prevents unbounded memory from large groups.

## 16. Dependencies

Runtime:

- C++17 standard library only.
- Parallel header additionally uses `std::thread`, `std::mutex`, and `std::condition_variable`.

Build:

- CMake.
- Threads package for parallel targets.
- No Boost, TBB, OpenMP, pdqsort, ska_sort, TimSort, or other sorting library.

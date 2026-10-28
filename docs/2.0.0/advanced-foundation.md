# 2.0 advanced scheduler foundation

Status: experimental, not installed, not a release.

## Public files

```text
include/oblivone/advanced/engine.hpp
include/oblivone/advanced/level.hpp
include/oblivone/advanced/options.hpp
include/oblivone/advanced/partition.hpp
include/oblivone/advanced/sort.hpp
tests/test_advanced.cpp
```

## Engine

```text
per-worker deques
three lanes: outer / inner / group
local LIFO, steal FIFO
idle observer registration
dynamic high-demand reservation
atomic lane queue counters
phase-reusable wait()
exception propagation
```

## Level allocator

```text
worker_count and slot_count separated
donate() / raise() idempotent team growth
60% floor
1.2 * mean oversized test
1.6 * target oversized test
multi-span plans
```

## Advanced sort

```text
parallel min/max
non-blocking root count/scatter continuations
parallel copy back
pump-based group allocation
worker-slot scratch reuse with scratch_budget
idle observer + pump T0 raise
recursive child partition for large spans
proportional child team with outer reservation
executor reuse
node and byte admission
opt-in sampled quantile boundaries
```

Child team sizing:

```text
if len <= 1.2 * plan.floor:
    team = team_for_child()
else:
    team = 1 + floor((len - 1.2 * plan.floor) / plan.floor)
    team = min(team, max_child_team())
```

`max_child_team()` leaves at least one worker, or the configured `reserved`
workers, outside the child:

```text
max_child_team = max(1, N - max(1, reserved))
```

`scratch_budget` mirrors simple parallel: 0 means `n * sizeof(T)`. It limits
the number of concurrently reused group scratch buffers.

`sample_buckets` is off by default. It matches the existing test_myself opt-in
experiment; benchmark numbers are worse, so it is not a default.

Group tasks are no longer precomputed at complete_scatter: N pumps pull plans
from the allocator and raise T0 before each `next()` call. The observer removes
itself when the last pump exits.

## Executor and budgets

```cpp
oblivone::advanced::executor ex(12);
oblivone::advanced::parallel_sort(ex, values, oblivone::options{}, popt);
```

Options:

```text
max_nodes       default 2 * threads
memory_budget   0 = unlimited
scratch_budget  0 = n * sizeof(T)
reserved        default 1
```

Root or child admission failure falls back to serial sorting for that span.
After an exception escapes, recreate the executor.

## Benchmark

MSVC Release:

### 1e6, 4 threads

| pattern | simple parallel | advanced | adv/simple |
|---|---:|---:|---:|
| random | 17.79 | 16.89 | 0.949 |
| few | 10.49 | 10.48 | 0.999 |
| organ | 14.38 | 14.97 | 1.041 |

### 1e7, 12 threads

| pattern | simple parallel | advanced | adv/simple |
|---|---:|---:|---:|
| random | 92.69 | 90.63 | 0.978 |
| few | 55.30 | 60.57 | 1.095 |
| organ | 91.72 | 94.77 | 1.033 |

Parallel copy, worker-slot scratch reuse, and proportional child teams leave the
random path faster than simple. few remains slower.

Sampled boundaries are opt-in and currently regress:

```text
1e7 random: 234.5 ms vs 99.9 ms default
1e7 few:     79.6 ms vs 60.7 ms default
1e7 organ:  113.9 ms vs 100.3 ms default
```

Keep `sample_buckets = false` unless explicitly experimenting.

## Local validation

```text
ctest 7/7
test_advanced passed
ASan passed
GCC test_advanced passed
30 repeated test_advanced runs passed
executor reuse passed
memory/node admission fallback passed
outer reservation assertion passed
fuzz_advanced 20,000+ cases passed
fuzz_advanced ASan 1,000 cases passed
fuzz_advanced GCC 3,000 cases passed
```

## TSan

CI workflow builds and runs:

```text
test_parallel
test_advanced
```

under `-fsanitize=thread` on Ubuntu.

## Missing

```text
per-level reservation
profiling of recursive/engine overhead
public advanced_sort promotion
```

The advanced layer is not installed and not part of the stable parallel API.

# OblivoneSort 1.1.0 — Cache Design

## Goal

1.1.0 adds an opt-in bucket-index cache. The default path is unchanged.

The cache stores each element's bucket id during the count pass and reuses it
during scatter, avoiding a second `key(value)` + `bucket_of(key)` calculation.

## Constraint

```text
B = max(1, min(m, bucket_limit))
cache is used only when B <= 65536
```

A `uint16_t` table is used. Larger bucket counts automatically fall back to the
uncached path.

## Serial path

`sort_range` accepts an optional `std::vector<Index>*`. When present:

- count writes `index[i]`;
- scatter reads `index[i]`;
- recursive groups share the same table.

The parent scatter is complete before child recursion, so child groups may
overwrite the first `m` entries.

## Parallel path

Only the root is cached. Root count writes the shared table; root scatter reads
it. Group recursion does not use the shared table, avoiding cross-thread writes.

If the parallel API falls back to serial because `n < min_n` or only one thread
is available, the serial dispatcher applies the same option.

## API

```cpp
oblivone::options opt;
opt.cache_bucket_index = true;
```

No new public functions; the flag applies to `sort`, `sort_by`,
`parallel_sort`, and `parallel_sort_by`.

## Memory

About `2n` extra bytes. Measured peak increase: about 12% at 1e7 and 1e8.

## Performance

- Expensive custom keys: consistently faster.
- Default arithmetic keys: compiler- and type-dependent; may regress.
- Parallel default keys: may regress.
- 1e8: no gain; memory bandwidth dominates.

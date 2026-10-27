# Strong-Algorithm Comparison

This page clarifies where OblivoneSort 1.1.0 sits among strong sorting
algorithms. It is not intended as a universal benchmark claim.

## Algorithms

Serial:

- `std::sort`
- `std::stable_sort`
- Boost 1.89 `pdqsort`
- Boost 1.89 `spreadsort`
- TimSort (cpp-TimSort `gfx::timsort`)

Parallel:

- Boost 1.89 `sample_sort`
- Boost 1.89 `parallel_stable_sort`
- Boost 1.89 `block_indirect_sort`
- OblivoneSort `parallel_sort`

## Test setup

```text
GCC 15.2 / MSVC 19.39
13 patterns
1e6: 4 threads
1e7: 12 threads
shuffled algorithm order per round
every mode verified sorted
```

Boost and TimSort are test-only dependencies, not runtime dependencies of
OblivoneSort.

## 1e7 highlights (GCC)

| pattern | fastest | ms | Oblivone single | Oblivone parallel |
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

MSVC shows the same picture: Boost parallel block/sample sorts lead most
patterns; TimSort leads organ/push.

## Conclusion

- Random/smooth serial: pdqsort and OblivoneSort are close.
- Random/smooth parallel: Boost `block_indirect_sort` is usually faster.
- Organ/equal/push: TimSort is strongest.
- Few/near-sorted: pdqsort or parallel block sort.
- OblivoneSort's value is simplicity, header-only use, and no third-party
  runtime dependency; it is not the fastest option in every pattern.
- If Boost/TBB is available, benchmark rather than assuming OblivoneSort wins.

## Cache

`cache_bucket_index` remains opt-in:

- default off;
- only `B <= 65536`;
- about `2n` extra bytes;
- most useful for expensive custom keys;
- no guarantee for default keys or against strong parallel algorithms.

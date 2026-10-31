# OblivoneSort 2.0.1 Test Record

> **Note:** This document is a translation and may contain errors;
> the Chinese documents under `docs/docs_2_0_1/` are authoritative.

## 1. Environment and method

- Compiler: GCC 15.2 (MinGW, `-O2`, `-std=c++20`)
- Platform: Windows 11, Intel i5-13500H
- Data: `double`, 13 patterns, fixed seeds:

```text
random / sorted / reverse / organ / sawtooth / runs / equal / few
near 0.1% / near 1% / near 5% / push front / push back
```

- Sizes and threads: 1e6 (serial 1 thread; parallel 4 threads), 1e7 (serial 1 thread; parallel 12 threads); plus 1e8 large-scale and special-data tests
- Method: 3 runs per combination, median; algorithm order rotated per pattern/rep; sortedness verified after every run; multiset equivalence verified element-wise for 1e7 and below
- Compared algorithms:

```text
serial    OblivoneSort, std::sort, std::stable_sort, Boost pdqsort,
          Boost spreadsort, cpp-TimSort
parallel  OblivoneSort simple, OblivoneSort advanced, Boost block_indirect_sort,
          Boost sample_sort, Boost parallel_stable_sort
```

Boost and cpp-TimSort are test-only dependencies, not runtime dependencies.

`best/O` = fastest competitor time / Oblivone time; greater than 1 means Oblivone is faster, less than 1 means the competitor is faster.

## 2. Serial results (ms)

### n = 1e6 (1 thread)

| pattern | oblivone | std_sort | std_stable | pdqsort | spreadsort | timsort | best/O |
|---|---:|---:|---:|---:|---:|---:|---:|
| random | 23.473 | 81.72 | 89.08 | 78.45 | 26.38 | 108.1 | 1.12 |
| sorted | 1.742 | 31.21 | 19.26 | 3.28 | 0.99 | 1.21 | 0.57 |
| reverse | 1.585 | 22.6 | 20.13 | 4.37 | 11.99 | 1.95 | 1.23 |
| organ | 17.924 | 100.63 | 20.73 | 50.19 | 21.23 | 4 | 0.22 |
| sawtooth | 16.897 | 43.34 | 25.69 | 28.7 | 12.77 | 13.72 | 0.76 |
| runs | 21.447 | 68.86 | 55.34 | 62.49 | 26.59 | 49.72 | 1.24 |
| equal | 1.405 | 20.76 | 18.87 | 2.65 | 1.06 | 1.26 | 0.75 |
| few | 9.464 | 29.57 | 29.32 | 7.5 | 8.29 | 19.77 | 0.79 |
| near_0.1% | 18.915 | 31.81 | 24.85 | 16 | 12.84 | 8.35 | 0.44 |
| near_1% | 18.969 | 33.45 | 29.37 | 22.24 | 15.58 | 13.78 | 0.73 |
| near_5% | 19.891 | 39.7 | 35.06 | 31.25 | 18.75 | 28.55 | 0.94 |
| push_front | 10.262 | 22.52 | 21.02 | 28.86 | 9.31 | 2.3 | 0.22 |
| push_back | 14.829 | 121.1 | 19.48 | 8.86 | 7.13 | 1.91 | 0.13 |

### n = 1e7 (1 thread)

| pattern | oblivone | std_sort | std_stable | pdqsort | spreadsort | timsort | best/O |
|---|---:|---:|---:|---:|---:|---:|---:|
| random | 243.319 | 973.52 | 1487.89 | 1287.1 | 572.95 | 1704.68 | 2.35 |
| sorted | 15.755 | 396.54 | 275.94 | 31.18 | 8.99 | 14.9 | 0.57 |
| reverse | 17.963 | 287.12 | 288.43 | 42.39 | 148.08 | 18.42 | 1.03 |
| organ | 220.812 | 1388.7 | 305.56 | 589.85 | 373.23 | 41.82 | 0.19 |
| sawtooth | 199.016 | 462.69 | 352.19 | 303.43 | 160.49 | 170.76 | 0.81 |
| runs | 240.519 | 834.08 | 758.92 | 766.69 | 398.54 | 667.51 | 1.66 |
| equal | 15.932 | 274.22 | 287.51 | 28.39 | 8.32 | 12.75 | 0.52 |
| few | 97.332 | 340.93 | 387.28 | 72.4 | 88.4 | 224.39 | 0.74 |
| near_0.1% | 194.942 | 395.6 | 350.91 | 206.85 | 180.2 | 120.65 | 0.62 |
| near_1% | 192.873 | 406.18 | 383.51 | 268.93 | 210.08 | 158.03 | 0.82 |
| near_5% | 204.044 | 460.42 | 451.14 | 370.58 | 260.56 | 320.21 | 1.28 |
| push_front | 110.068 | 293 | 309.48 | 330.15 | 124.03 | 21.07 | 0.19 |
| push_back | 135.292 | 1530.2 | 301.38 | 92 | 68.31 | 20.59 | 0.15 |

## 3. Parallel results (ms)

### n = 1e6 (4 threads)

| pattern | simple | advanced | boost_block | boost_sample | boost_stable | best/O |
|---|---:|---:|---:|---:|---:|---:|
| random | 8.137 | 9.996 | 29.873 | 27.926 | 30.533 | 1.23 |
| sorted | 1.468 | 1.423 | 1.348 | 1.397 | 1.303 | 0.89 |
| reverse | 1.47 | 1.8 | 1.854 | 2.145 | 1.789 | 1.22 |
| organ | 8.199 | 8.999 | 23.826 | 3.912 | 5.207 | 0.48 |
| sawtooth | 6.458 | 7.203 | 17.77 | 8.287 | 9.807 | 1.12 |
| runs | 7.449 | 9.737 | 22.973 | 17.474 | 21.436 | 1.31 |
| equal | 1.402 | 1.684 | 1.585 | 1.286 | 1.556 | 0.92 |
| few | 5.535 | 5.485 | 17.151 | 9.991 | 11.524 | 0.99 |
| near_0.1% | 7.37 | 7.897 | 11.102 | 7.647 | 9.607 | 1.04 |
| near_1% | 7.963 | 8.059 | 13.645 | 9.14 | 10.664 | 1.01 |
| near_5% | 7.016 | 8.635 | 15 | 10.904 | 12.716 | 1.23 |
| push_front | 4.784 | 5.306 | 15.03 | 4.201 | 5.388 | 0.88 |
| push_back | 7.335 | 7.891 | 24.297 | 4.404 | 6.352 | 0.6 |

### n = 1e7 (12 threads)

| pattern | simple | advanced | boost_block | boost_sample | boost_stable | best/O |
|---|---:|---:|---:|---:|---:|---:|
| random | 55.6 | 56.087 | 143.306 | 135.179 | 178.382 | 2.58 |
| sorted | 17.281 | 15.998 | 14.186 | 14.073 | 14.177 | 0.81 |
| reverse | 17.727 | 17.473 | 20.412 | 19.888 | 21.039 | 1.12 |
| organ | 60.002 | 63.359 | 188.815 | 27.967 | 45.073 | 0.47 |
| sawtooth | 39.667 | 39.886 | 51.991 | 53.225 | 66.058 | 1.31 |
| runs | 53.777 | 55.659 | 114.754 | 99.45 | 140.732 | 1.85 |
| equal | 14.349 | 14.93 | 13.666 | 14.197 | 13.47 | 0.94 |
| few | 43.332 | 46.62 | 64.632 | 63.652 | 73.2 | 1.47 |
| near_0.1% | 47.271 | 47.974 | 82.811 | 53.29 | 67.585 | 1.13 |
| near_1% | 47.136 | 49.139 | 71.648 | 53.863 | 69.858 | 1.14 |
| near_5% | 47.981 | 49.398 | 84.483 | 62.586 | 80.771 | 1.30 |
| push_front | 40.587 | 40.627 | 46.595 | 20.092 | 35.739 | 0.50 |
| push_back | 68.549 | 64.783 | 238.67 | 34.291 | 48.635 | 0.50 |

## 4. Large scale (1e8)

### Serial (1 thread)

| pattern | oblivone | std_sort | other |
|---|---:|---:|---|
| random | 2982.1 | 10919.2 | pdqsort 10643.7 |
| sorted | 156.4 | 4576.7 | — |
| reverse | 179.9 | 3210.4 | — |
| equal | 173.6 | 3098.4 | — |
| few | 984.0 | 3904.1 | — |
| organ | 2042.0 | 16849.5 | — |
| sawtooth | 1719.3 | 5070.8 | — |
| near 5% | 1851.7 | 5468.7 | — |

### Parallel (12 threads)

| pattern | simple | advanced | boost_sample |
|---|---:|---:|---:|
| random | 641.9 | 683.4 | 1500.5 |
| few | 498.6 | 511.9 | 772.7 |
| organ | 751.9 | 782.1 | 262.4 |

## 5. Special and degenerate configurations

All cases below passed correctness checks (sortedness, plus multiset equivalence at 1e7 and below).

### 5.1 Degenerate configurations (1e7, 12 threads, random)

| configuration | backend | ms |
|---|---:|---:|
| default | simple | 55.6 |
| `max_buckets = 16` | simple | 79.5 |
| `scratch_budget = 1` | simple | 170.4 |
| `cache_bucket_index = true` | simple | 56.1 |
| `bucket_limit = 1, max_depth = 1` | simple | 82.6 |
| `max_nodes = 1` | advanced | 56.2 |
| `memory_budget = 1` | advanced | 286.3 |
| `sample_buckets = true` | advanced | 58.7 |

### 5.2 Special data

| case | n | result |
|---|---:|---|
| int64 extremes (MIN/MAX/0/±1) | 1e7 | 122.7 ms, element-wise identical to std::sort |
| NaN-heavy (1/3 NaN, ±Inf, ±0) | 1e7 | serial 237.5 ms; 12 threads 91.7 ms |
| denormal mix | 1e7 | 221.0 ms (std::sort 959.3 ms) |
| near duplicates (1000 values) | 1e7 | 206.2 ms (pdqsort 410.9, std::sort 575.1) |

### 5.3 Small sizes (ns per call, 200k calls averaged, serial)

| n | oblivone | std_sort |
|---:|---:|---:|
| 2 | 10 | 12 |
| 3 | 11 | 13 |
| 4 | 12 | 15 |
| 8 | 20 | 34 |
| 16 | 60 | 104 |
| 32 | 168 | 268 |
| 64 | 1248 | 584 |

### 5.4 Thread scaling (1e7, random, simple)

| threads | 1 | 2 | 4 | 12 |
|---|---:|---:|---:|---:|
| ms | 222.4 | 123.5 | 76.3 | 52.9 |

## 6. Conclusions

- Serial 1e7: random **2.35×**, runs **1.66×**, near 5% **1.28×** and reverse **1.03×** relative to the fastest competitor; the remaining patterns are slower, with organ, push front and push back at 0.15–0.19 relative to TimSort (TimSort is about 5–8× faster), and sorted / equal at 0.52–0.57 relative to spreadsort.
- Parallel 1e7: random **2.58×**, runs **1.85×**, few **1.47×**, sawtooth **1.31×**, near 5% **1.30×**; organ, push front and push back are 0.47–0.50 relative to boost_sample.
- 1e8 random serial: 2982 ms (std::sort 10919 ms); 1e8 random parallel 12 threads: 641.9 ms (boost_sample 1500.5 ms).
- At small sizes OblivoneSort is faster up to n = 32; from n = 64 std::sort takes the lead.
- All special-data and degenerate-configuration cases passed correctness checks; the `max_buckets < coarse_buckets` boundary configuration no longer shows abnormal degradation.

## 7. Raw data

- GCC 13-pattern matrix: `test-data/gcc.csv`
- Extreme and degenerate configurations: `test-data/extreme.csv`

This record was produced during the 2.0.1 pre-release test run; performance data applies only to the current machine and compiler combination.

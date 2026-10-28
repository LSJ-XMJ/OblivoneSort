# OblivoneSort

[![CI](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml/badge.svg)](https://github.com/LSJ-XMJ/OblivoneSort/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![DOI](https://zenodo.org/badge/1433127737.svg)](https://doi.org/10.5281/zenodo.23697924)

> **2.0.0-dev. Not released.**
>
> The released stable version is v1.1.0. This branch starts the 2.0 serial
> baseline and a new experimental parallel layer. No 2.0 tag or release exists.

C++17, header-only, non-stable distribution sort.

OblivoneSort maps values to an order-preserving `uint64_t` key, computes a
bucket directly from that key, then performs count, prefix sum, scatter, and
recursive or insertion sorting inside buckets.

> **Scope**: random/smooth numeric distributions.
>
> It does not target structured data; TimSort is stronger on organ/runs/push
> inputs.

Current development version: `2.0.0-dev`.

## Features

- Single-threaded `sort` / `sort_by`;
- experimental parallel `parallel_sort` / `parallel_sort_by`;
- reusable `thread_pool` for parallel calls;
- optional bucket-index cache (off by default);
- NaN ordered last for double/float, `-0 == +0`;
- default fallback uses insertion sort for small ranges;
- injectable custom key and fallback;
- no `<thread>` dependency in the core headers;
- no third-party runtime dependency;
- Apache-2.0, no patents.

## Quick start

```cpp
#include <oblivone/sort.hpp>

std::vector<double> values = ...;
oblivone::sort(values);
```

Custom key:

```cpp
struct Item {
    int score;
};

std::vector<Item> items = ...;
oblivone::sort_by(items, [](const Item& item) {
    return oblivone::key_of(item.score);
});
```

A custom key must be order-preserving and convertible to `uint64_t`.

## Parallel

The 2.0 parallel layer is a separate header:

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 12;
popt.min_n = 2000000;
popt.max_buckets = 65536;

oblivone::parallel_sort(values, oblivone::options{}, popt);
```

Reusing a pool:

```cpp
oblivone::thread_pool pool(12);

oblivone::parallel_options popt;
popt.pool = &pool;
oblivone::parallel_sort(values, oblivone::options{}, popt);
```

`sort` never creates threads. A custom key or fallback used through the parallel
API must be reentrant and thread-safe.

## Options

```cpp
oblivone::options opt;
opt.small_top      = 48;     // top-level n <= 48 -> fallback
opt.small_sort     = 16;     // core group m < 16 -> fallback
opt.leaf_sort      = 22;     // group m < 22 -> insertion
opt.scan_threshold = 256;    // nested scan; 0 = disabled
opt.max_depth      = 16;     // recursion limit
opt.bucket_limit   = 65536;  // per-level bucket limit
opt.cache_bucket_index = false;  // bucket-index cache
```

### Bucket-index cache

```cpp
oblivone::options opt;
opt.cache_bucket_index = true;
oblivone::sort_by(values, expensive_key, opt);
```

Rules:

- off by default;
- effective only when the number of buckets `B <= 65536`;
- uses a `uint16_t` table;
- `B > 65536` automatically disables the cache;
- requires about `2n` extra bytes;
- useful for expensive custom keys; default integer keys do not always benefit.

## Semantics

- not stable;
- all NaNs are placed after non-NaNs;
- `-0.0` and `+0.0` compare equal;
- `n > UINT32_MAX` goes directly to fallback;
- default fallback: insertion for arithmetic ranges with n <= 96, otherwise
  `std::sort`;
- a custom fallback must use the same `less`; worst case O(k log k);
- exceptions provide only the basic guarantee.

## Performance

The following compares only with `std::sort`, using GCC 15.2, `-O2`, and
median timings. Absolute values depend on CPU and thermal state.

### 1e6, serial

| pattern | std::sort | OblivoneSort |
|---|---:|---:|
| random | 83.6 ms | **24.3 ms** |
| sorted | 32.2 ms | **1.44 ms** |
| reverse | 22.5 ms | **1.66 ms** |
| organ | 101.8 ms | **19.5 ms** |
| sawtooth | 43.9 ms | **16.1 ms** |
| runs | 69.9 ms | **20.6 ms** |
| equal | 21.7 ms | **1.84 ms** |
| few | 29.1 ms | **10.4 ms** |
| near 1% | 33.8 ms | **19.7 ms** |
| push back | 121.5 ms | **15.3 ms** |

### 1e7, serial

| pattern | std::sort | OblivoneSort |
|---|---:|---:|
| random | 965.5 ms | **260.1 ms** |
| sorted | 408.4 ms | **16.1 ms** |
| organ | 1417.8 ms | **191.7 ms** |
| runs | 828.1 ms | **210.3 ms** |
| few | 344.9 ms | **99.7 ms** |
| near 1% | 422.4 ms | **202.1 ms** |
| push back | 1502.1 ms | **134.6 ms** |

### 1e7 parallel, 12 threads

| pattern | serial | parallel | speedup |
|---|---:|---:|---:|
| random | 422.1 ms | **93.5 ms** | 4.51× |
| few | 163.6 ms | **55.7 ms** | 2.94× |
| organ | 244.9 ms | **99.9 ms** | 2.45× |

The parallel layer is experimental in 2.0.0-dev. TSan is required before it is
called stable.

### Boundaries

These results are not a claim of being fastest in every case:

- random/smooth distributions are the main strength;
- organ/runs/push: TimSort can be faster;
- few/near-sorted: pdqsort/Boost spreadsort can be faster;
- cache benefit disappears at very large n;
- the new parallel layer is experimental and still needs TSan.

See the historical v1.1.0 benchmark documentation for parallel comparisons.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Install:

```bash
cmake --install build --prefix /your/prefix
```

CMake target:

```cmake
target_link_libraries(app PRIVATE oblivone::oblivone)   # serial
target_link_libraries(app PRIVATE oblivone::parallel)   # parallel, links Threads
```

The serial target does not link Threads.

## Suitable / not suitable

Suitable:

- random floating-point or integer data;
- relatively smooth distributions;
- large n with a header-only, no-third-party-dependency requirement;
- environments where the serial core must not create threads.

Not suitable:

- mostly sorted or near-sorted data;
- many runs/structured data;
- stable-sort requirements;
- no extra `n` buffer available;
- very small n (plain `std::sort` is simpler).

## Documentation

- v1.1.0 Chinese docs: [docs/docs_1_1_0/README.md](docs/docs_1_1_0/README.md)
- v1.1.0 English docs: [Code Design](docs/docs_1_1_0_EN/design.md) · [Usage](docs/docs_1_1_0_EN/usage.md) · [Benchmarks](docs/docs_1_1_0_EN/benchmarks.md)

These documents describe the released v1.1.0 and its parallel API.

## Author

肖明杰.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

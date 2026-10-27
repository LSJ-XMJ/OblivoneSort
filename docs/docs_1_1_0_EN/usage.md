# OblivoneSort 1.1.0 — Usage

## Enable the cache

```cpp
#include <oblivone/sort.hpp>

oblivone::options opt;
opt.cache_bucket_index = true;

oblivone::sort(values, opt);
oblivone::sort_by(values, expensive_key, opt);
```

Parallel APIs use the same option:

```cpp
#include <oblivone/parallel.hpp>

oblivone::parallel_options popt;
popt.threads = 8;

oblivone::parallel_sort(values, opt, popt);
oblivone::parallel_sort_by(values, expensive_key, opt, popt);
```

## Rules

- Default is `false`.
- Applies only when the effective bucket count is `<= 65536`.
- Larger bucket counts silently use the uncached path.
- Adds about `2n` bytes of memory.
- Does not change sorting semantics.
- Cached serial recursion and parallel root are supported; parallel groups are
  not cached.

## When to use

Use it for expensive custom keys. For default integer keys, benchmark first.
For parallel default keys and 1e8 inputs, prefer leaving it off.

## Example

```cpp
struct Item {
    std::uint64_t rank;
    double payload;
};

oblivone::options opt;
opt.cache_bucket_index = true;

oblivone::sort_by(items, [](const Item& item) {
    return item.rank;
}, opt);
```

## Semantics

- unstable sort;
- NaN last;
- `-0 == +0`;
- custom key/fallback/thread-pool rules are unchanged.

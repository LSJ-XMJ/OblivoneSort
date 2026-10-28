# OblivoneSort 2.0.0 — testing notes

Status: release candidate prepared. Tag, GitHub Release and Zenodo archive are
still pending repository-owner actions.

The official parallel API is the simple phase-pool layer. The advanced
scheduler is experimental and is excluded from install.

## Implemented parallel API

```text
include/oblivone/parallel.hpp

oblivone::thread_pool
oblivone::parallel_options
oblivone::parallel_sort
oblivone::parallel_sort_by
```

CMake:

```text
oblivone            serial
oblivone_parallel   serial + Threads
```

## Local test result

MSVC Release, Windows:

```text
1/7 test_sort      Passed
2/7 fuzz_sort      Passed
3/7 test_cache     Passed
4/7 fuzz_cache     Passed
5/7 test_parallel  Passed
6/7 test_advanced  Passed
7/7 fuzz_advanced  Passed

100% tests passed
```

`test_parallel` covers:

```text
random
sorted
reverse
few
NaN / ±0 / ±infinity
cache path
sort_by
thread_pool reuse
multiset equality
```

## ASan

MSVC Debug + ASan:

```text
test_parallel passed
ASan exit 0
```

## Packaging

Installed:

```text
include/oblivone/parallel.hpp
oblivone::parallel target
find_dependency(Threads)
```

A consumer linked against `oblivone::parallel` built and ran successfully.

## Benchmark

MSVC Release:

### 1e6, 4 threads

| pattern | serial | parallel | ratio |
|---|---:|---:|---:|
| random | 40.8 | 18.6 | 0.455 |
| few | 16.5 | 10.9 | 0.659 |
| organ | 24.5 | 14.5 | 0.593 |

### 1e7, 12 threads

| pattern | serial | parallel | ratio |
|---|---:|---:|---:|
| random | 422.1 | 93.5 | 0.222 |
| few | 163.6 | 55.7 | 0.341 |
| organ | 244.9 | 99.9 | 0.408 |

## TSan

GitHub Actions `ubuntu-latest` with `libtsan0`.

Status: **passed** for the release candidate.

Scope:

```text
public 2.0 parallel.hpp / thread_pool
public test_parallel
advanced engine/allocator/pump/observer
test_advanced
fuzz_advanced
```

The public simple parallel layer is the release gate. The advanced scheduler
remains experimental even though it is covered by the same workflow.

## Release status

```text
version header     2.0.0
CMake project      2.0.0
tag                pending
GitHub Release     pending
Zenodo DOI         pending
```

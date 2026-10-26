// EXPERIMENTAL / TEST MOD — not the final parallel API.
// Separate header so the core sort.hpp stays clean.
//
// Parallel root count/scatter/copy + depth-1 groups.
// Requires -pthread (or Threads::Threads on CMake).

#ifndef OBLIVONE_PARALLEL_EXPERIMENTAL_HPP
#define OBLIVONE_PARALLEL_EXPERIMENTAL_HPP

#include "sort.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <type_traits>
#include <vector>

namespace oblivone {
namespace experimental {

struct parallel_options {
    unsigned threads = 0;                 // 0 = auto
    std::size_t min_n = 1000000;          // below this, use serial
    std::size_t max_buckets = 65536;      // per-thread tables; keep this small
};

namespace detail2 {

inline unsigned auto_threads(std::size_t n) {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 4;
    if (n < 10000000) return (hw > 4) ? 4 : hw;
    return (hw > 12) ? 12 : hw;
}

template <class F>
void run_threads(unsigned T, F&& fn) {
    std::vector<std::thread> threads;
    threads.reserve(T);
    for (unsigned t = 0; t < T; ++t) {
        threads.emplace_back([&, t] { fn(t); });
    }
    for (auto& th : threads) th.join();
}

template <class T, class KeyFn, class LessFn, class Fallback>
void parallel_impl(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
                   bool native_less, const options& opt,
                   const parallel_options& popt, Fallback fallback) {
    if (n < 2) return;

    const unsigned nthreads = popt.threads ? popt.threads : auto_threads(n);
    if (nthreads <= 1 || n < popt.min_n) {
        xsort_by(data, n, key, opt, fallback);
        return;
    }
    if (n > 0xFFFFFFFFull) {
        fallback(data, data + n, less);
        return;
    }
    if (n <= opt.small_top) {
        fallback(data, data + n, less);
        return;
    }

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = 1; i < n; ++i) {
        const auto a = key(data[i - 1]);
        const auto b = key(data[i]);
        if (a > b) ascending = false;
        if (a < b) descending = false;
        if (!ascending && !descending) break;
    }
    if (ascending) return;
    if (descending) {
        std::reverse(data, data + n);
        return;
    }
    if (detail::budgeted_insertion(data, n, key, n / 8)) return;

    std::uint64_t mn = key(data[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = key(data[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    const std::size_t auto_cap =
        opt.bucket_cap
            ? opt.bucket_cap
            : ((n <= 2000000) ? (std::size_t(1) << 16)
                              : (std::size_t(1) << 20));
    const std::size_t cap =
        (std::min)((std::min)(opt.max_buckets, auto_cap), popt.max_buckets);
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(n, cap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double x = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(x);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::vector<std::uint32_t>> local(
        nthreads, std::vector<std::uint32_t>(B, 0));
    run_threads(nthreads, [&](unsigned t) {
        const std::size_t lo = n * t / nthreads;
        const std::size_t hi = n * (t + 1) / nthreads;
        auto& cnt = local[t];
        for (std::size_t i = lo; i < hi; ++i) ++cnt[bucket_of(key(data[i]))];
    });

    std::vector<std::uint32_t> total(B, 0);
    for (unsigned t = 0; t < nthreads; ++t) {
        for (std::size_t b = 0; b < B; ++b) total[b] += local[t][b];
    }
    std::vector<std::uint32_t> start(B), end(B);
    std::uint32_t running = 0;
    std::size_t max_group = 0;
    for (std::size_t b = 0; b < B; ++b) {
        start[b] = running;
        running += total[b];
        end[b] = running;
        if (total[b] > max_group) max_group = total[b];
    }

    std::vector<std::vector<std::uint32_t>> off(
        nthreads, std::vector<std::uint32_t>(B, 0));
    std::vector<std::uint32_t> cur = start;
    for (unsigned t = 0; t < nthreads; ++t) {
        for (std::size_t b = 0; b < B; ++b) {
            off[t][b] = cur[b];
            cur[b] += local[t][b];
        }
    }
    local.clear();
    local.shrink_to_fit();

    std::vector<T> buf(n);
    run_threads(nthreads, [&](unsigned t) {
        const std::size_t lo = n * t / nthreads;
        const std::size_t hi = n * (t + 1) / nthreads;
        auto& o = off[t];
        for (std::size_t i = lo; i < hi; ++i) {
            const std::size_t b = bucket_of(key(data[i]));
            buf[o[b]++] = data[i];
        }
    });
    run_threads(nthreads, [&](unsigned t) {
        const std::size_t lo = n * t / nthreads;
        const std::size_t hi = n * (t + 1) / nthreads;
        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(lo),
                  buf.begin() + static_cast<std::ptrdiff_t>(hi), data + lo);
    });

    std::atomic<std::size_t> next{0};
    run_threads(nthreads, [&](unsigned) {
        std::vector<T> scratch(max_group);
        for (;;) {
            const std::size_t b = next.fetch_add(1, std::memory_order_relaxed);
            if (b >= B) break;
            const std::uint32_t len = end[b] - start[b];
            if (len > 1) {
                if (len < opt.leaf_sort) {
                    if (native_less) {
                        oblivone::detail::insertion_sort_less(data + start[b], len,
                                                              less);
                    } else {
                        oblivone::detail::insertion_sort(data + start[b], len, key);
                    }
                } else {
                    oblivone::detail::sort_range(data + start[b], len, 1, key,
                                                 less, native_less, fallback,
                                                 opt, scratch);
                }
            }
        }
    });
}

}  // namespace detail2

template <class T, class Fallback = default_fallback>
void parallel_sort(T* data, std::size_t n,
                   const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& v) { return detail::to_key(v); };
    if constexpr (std::is_same<T, double>::value) {
        detail2::parallel_impl(data, n, key, detail::less_double, true, opt, popt,
                               fallback);
    } else {
        auto less = [&](const T& x, const T& y) { return key(x) < key(y); };
        detail2::parallel_impl(data, n, key, less, false, opt, popt, fallback);
    }
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(T* data, std::size_t n, KeyFn key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    auto less = [&](const T& x, const T& y) { return key(x) < key(y); };
    detail2::parallel_impl(data, n, key, less, false, opt, popt, fallback);
}

template <class T, class Fallback = default_fallback>
void parallel_sort(std::vector<T>& v, const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    parallel_sort(v.data(), v.size(), opt, popt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(std::vector<T>& v, KeyFn key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    parallel_sort_by(v.data(), v.size(), key, opt, popt, fallback);
}

}  // namespace experimental
}  // namespace oblivone

#endif  // OBLIVONE_PARALLEL_EXPERIMENTAL_HPP

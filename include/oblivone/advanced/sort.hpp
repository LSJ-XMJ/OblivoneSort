#ifndef OBLIVONE_ADVANCED_SORT_HPP
#define OBLIVONE_ADVANCED_SORT_HPP

#include <oblivone/sort.hpp>

#include <oblivone/detail/coarse.hpp>

#include "engine.hpp"
#include "options.hpp"
#include "partition.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace oblivone {
namespace advanced {
namespace detail {

inline unsigned choose_threads(std::size_t n) {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 4;
    if (n < 10000000) return (hw > 4) ? 4 : hw;
    return (hw > 12) ? 12 : hw;
}

template <class T, class KeyFn, class LessFn, class Fallback>
void run(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
         const oblivone::options& sort_opt, const options& par_opt,
         Fallback fallback, engine* external = nullptr) {
    if (n < 2) return;

    unsigned threads =
        par_opt.threads ? par_opt.threads : choose_threads(n);
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw != 0 && threads > hw) threads = hw;
    if (external) threads = external->size();
    if (threads > n) threads = static_cast<unsigned>(n);

    if (threads <= 1 || n < par_opt.min_n) {
        oblivone::detail::sort_impl(data, n, key, less, sort_opt, fallback);
        return;
    }
    if (!oblivone::detail::prepare(data, n, key, less, sort_opt, fallback)) {
        return;
    }

    std::unique_ptr<engine> local;
    engine* eng = external;
    if (!eng) {
        local = std::make_unique<engine>(threads);
        eng = local.get();
        eng->start();
    }
    eng->reserve(par_opt.reserved);
    eng->set_node_limit(par_opt.max_nodes
                            ? par_opt.max_nodes
                            : (std::max)(std::size_t(2),
                                         static_cast<std::size_t>(threads) * 2));
    eng->set_memory_limit(par_opt.memory_budget);

    std::vector<std::uint64_t> mins(threads);
    std::vector<std::uint64_t> maxs(threads);
    std::vector<T> value_mins;
    std::vector<T> value_maxs;
    std::vector<unsigned char> value_nan(threads, 0);
    if constexpr (std::is_arithmetic<T>::value) {
        value_mins.resize(threads);
        value_maxs.resize(threads);
    }
    for (unsigned t = 0; t < threads; ++t) {
        eng->submit(
            [&, t] {
                const std::size_t lo = n * t / threads;
                const std::size_t hi = n * (t + 1) / threads;
                std::uint64_t mn = key(data[lo]);
                std::uint64_t mx = mn;
                if constexpr (std::is_arithmetic<T>::value) {
                    T vmn = data[lo];
                    T vmx = vmn;
                    bool nan = false;
                    for (std::size_t i = lo + 1; i < hi; ++i) {
                        if constexpr (std::is_floating_point<T>::value) {
                            if (std::isnan(data[i])) {
                                nan = true;
                            } else {
                                vmn = (std::min)(vmn, data[i]);
                                vmx = (std::max)(vmx, data[i]);
                            }
                        } else {
                            vmn = (std::min)(vmn, data[i]);
                            vmx = (std::max)(vmx, data[i]);
                        }
                        const std::uint64_t k = key(data[i]);
                        if (k < mn) mn = k;
                        if (k > mx) mx = k;
                    }
                    value_mins[t] = vmn;
                    value_maxs[t] = vmx;
                    value_nan[t] = nan ? 1 : 0;
                } else {
                    for (std::size_t i = lo + 1; i < hi; ++i) {
                        const std::uint64_t k = key(data[i]);
                        if (k < mn) mn = k;
                        if (k > mx) mx = k;
                    }
                }
                mins[t] = mn;
                maxs[t] = mx;
            },
            0, static_cast<int>(t));
    }
    eng->wait();

    std::uint64_t minimum = mins[0];
    std::uint64_t maximum = maxs[0];
    for (unsigned t = 1; t < threads; ++t) {
        minimum = (std::min)(minimum, mins[t]);
        maximum = (std::max)(maximum, maxs[t]);
    }
    if (minimum == maximum) return;

    oblivone::detail::value_bucket_map value_map;
    if constexpr (std::is_arithmetic<T>::value) {
        T vmin = value_mins[0];
        T vmax = value_maxs[0];
        bool nan = value_nan[0] != 0;
        for (unsigned t = 1; t < threads; ++t) {
            nan = nan || value_nan[t] != 0;
            vmin = (std::min)(vmin, value_mins[t]);
            vmax = (std::max)(vmax, value_maxs[t]);
        }
        if (!nan) {
            const std::size_t coarse =
                par_opt.coarse_buckets
                    ? par_opt.coarse_buckets
                    : oblivone::detail::automatic_coarse_buckets(n);
            value_map = oblivone::detail::make_value_bucket_map(
                static_cast<double>(vmin), static_cast<double>(vmax),
                (std::max)(std::size_t(1), (std::min)(n, coarse)));
        }
    }

    oblivone::options effective = sort_opt;
    std::size_t buckets = value_map.enabled
                              ? value_map.buckets
                              : (effective.bucket_limit
                                     ? effective.bucket_limit
                                     : 1);
    if (par_opt.max_buckets) {
        buckets = (std::min)(buckets, par_opt.max_buckets);
    }
    buckets = (std::max)(std::size_t(1), buckets);
    value_map.buckets = buckets;
    if (value_map.enabled) {
        effective.bucket_limit =
            par_opt.local_bucket_limit
                ? par_opt.local_bucket_limit
                : oblivone::detail::automatic_local_buckets(n);
    }

    auto node = std::make_shared<
        root_node<T, KeyFn, LessFn, Fallback>>(
        *eng, data, n, threads, buckets, 0,
        (std::max)(std::size_t(1024),
                   static_cast<std::size_t>(n / threads)),
        minimum, maximum, key, less, fallback, effective, par_opt,
        value_map);
    if (!node->admitted()) {
        oblivone::detail::sort_impl(data, n, key, less, effective, fallback);
        return;
    }
    node->start();
    eng->wait();
}

}  // namespace detail

// Reusable advanced engine. One caller thread at a time; after an exception
// escapes, recreate it.
class executor {
public:
    explicit executor(unsigned threads = 0)
        : eng_(threads ? threads : default_threads()) {
        eng_.start();
    }

    ~executor() { eng_.stop(); }

    executor(const executor&) = delete;
    executor& operator=(const executor&) = delete;

    unsigned size() const { return eng_.size(); }
    engine& get() { return eng_; }

private:
    static unsigned default_threads() {
        const unsigned hw = std::thread::hardware_concurrency();
        return hw ? hw : 4;
    }

    engine eng_;
};

template <class T, class Fallback = default_fallback>
void parallel_sort(T* data, std::size_t n,
                   const oblivone::options& sort_opt = {},
                   const options& par_opt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& value) { return oblivone::detail::key_of(value); };
    if constexpr (std::is_same<T, double>::value) {
        auto less = [](const T& a, const T& b) {
            return oblivone::detail::less_double(a, b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback);
    } else if constexpr (std::is_same<T, float>::value) {
        auto less = [](const T& a, const T& b) {
            return oblivone::detail::less_float(a, b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback);
    } else {
        auto less = [&key](const T& a, const T& b) {
            return key(a) < key(b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback);
    }
}

template <class T, class Fallback = default_fallback>
void parallel_sort(executor& ex, T* data, std::size_t n,
                   const oblivone::options& sort_opt = {},
                   const options& par_opt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& value) { return oblivone::detail::key_of(value); };
    if constexpr (std::is_same<T, double>::value) {
        auto less = [](const T& a, const T& b) {
            return oblivone::detail::less_double(a, b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback, &ex.get());
    } else if constexpr (std::is_same<T, float>::value) {
        auto less = [](const T& a, const T& b) {
            return oblivone::detail::less_float(a, b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback, &ex.get());
    } else {
        auto less = [&key](const T& a, const T& b) {
            return key(a) < key(b);
        };
        detail::run(data, n, key, less, sort_opt, par_opt, fallback, &ex.get());
    }
}

template <class T, class Fallback = default_fallback>
void parallel_sort(std::vector<T>& values, const oblivone::options& sort_opt = {},
                   const options& par_opt = {},
                   Fallback fallback = Fallback{}) {
    parallel_sort(values.data(), values.size(), sort_opt, par_opt, fallback);
}

template <class T, class Fallback = default_fallback>
void parallel_sort(executor& ex, std::vector<T>& values,
                   const oblivone::options& sort_opt = {},
                   const options& par_opt = {},
                   Fallback fallback = Fallback{}) {
    parallel_sort(ex, values.data(), values.size(), sort_opt, par_opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(T* data, std::size_t n, KeyFn user_key,
                      const oblivone::options& sort_opt = {},
                      const options& par_opt = {},
                      Fallback fallback = Fallback{}) {
    auto key = [&user_key](const T& value) -> std::uint64_t {
        return static_cast<std::uint64_t>(user_key(value));
    };
    auto less = [&key](const T& a, const T& b) { return key(a) < key(b); };
    detail::run(data, n, key, less, sort_opt, par_opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(executor& ex, T* data, std::size_t n, KeyFn user_key,
                      const oblivone::options& sort_opt = {},
                      const options& par_opt = {},
                      Fallback fallback = Fallback{}) {
    auto key = [&user_key](const T& value) -> std::uint64_t {
        return static_cast<std::uint64_t>(user_key(value));
    };
    auto less = [&key](const T& a, const T& b) { return key(a) < key(b); };
    detail::run(data, n, key, less, sort_opt, par_opt, fallback, &ex.get());
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(std::vector<T>& values, KeyFn key,
                      const oblivone::options& sort_opt = {},
                      const options& par_opt = {},
                      Fallback fallback = Fallback{}) {
    parallel_sort_by(values.data(), values.size(), key, sort_opt, par_opt,
                     fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(executor& ex, std::vector<T>& values, KeyFn key,
                      const oblivone::options& sort_opt = {},
                      const options& par_opt = {},
                      Fallback fallback = Fallback{}) {
    parallel_sort_by(ex, values.data(), values.size(), key, sort_opt, par_opt,
                     fallback);
}

}  // namespace advanced
}  // namespace oblivone

#endif  // OBLIVONE_ADVANCED_SORT_HPP

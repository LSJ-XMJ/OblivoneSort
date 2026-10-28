#ifndef OBLIVONE_PARALLEL_HPP
#define OBLIVONE_PARALLEL_HPP

#include "sort.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace oblivone {

// Reusable worker pool for parallel_sort.
//
// run() publishes one phase of indexed tasks. A task must not call run() on the
// same pool; phases are synchronous.
class thread_pool {
public:
    explicit thread_pool(unsigned threads = 0) {
        n_ = threads ? threads : std::thread::hardware_concurrency();
        if (n_ == 0) n_ = 1;
        workers_.reserve(n_);
        for (unsigned i = 0; i < n_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~thread_pool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        work_cv_.notify_all();
        for (auto& worker : workers_) worker.join();
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    unsigned size() const { return n_; }

    template <class Fn>
    void run(unsigned tasks, Fn fn) {
        if (tasks == 0) return;
        std::unique_lock<std::mutex> phase_lock(phase_mutex_);

        auto state = std::make_shared<phase>();
        state->fn = std::function<void(unsigned)>(std::move(fn));
        state->total = tasks;
        state->remaining.store(tasks);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            phase_ = state;
            ++generation_;
            error_ = nullptr;
        }
        work_cv_.notify_all();

        {
            std::unique_lock<std::mutex> lock(mutex_);
            done_cv_.wait(lock, [&] { return state->remaining.load() == 0; });
        }

        std::exception_ptr error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            error = error_;
            error_ = nullptr;
            phase_.reset();
        }
        if (error) std::rethrow_exception(error);
    }

private:
    struct phase {
        std::function<void(unsigned)> fn;
        unsigned total = 0;
        std::atomic<unsigned> next{0};
        std::atomic<unsigned> remaining{0};
    };

    void worker_loop() {
        unsigned seen = 0;
        for (;;) {
            std::shared_ptr<phase> state;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_cv_.wait(lock, [&] {
                    return stop_ || generation_ != seen;
                });
                if (stop_) return;
                seen = generation_;
                state = phase_;
            }
            if (!state) continue;

            for (;;) {
                const unsigned index =
                    state->next.fetch_add(1, std::memory_order_relaxed);
                if (index >= state->total) break;
                try {
                    state->fn(index);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!error_) error_ = std::current_exception();
                }
                if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) ==
                    1) {
                    // Publish completion under the wait mutex; otherwise the
                    // notifier can run between predicate evaluation and sleep.
                    std::lock_guard<std::mutex> lock(mutex_);
                    done_cv_.notify_all();
                }
            }
        }
    }

    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex phase_mutex_;
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    std::shared_ptr<phase> phase_;
    unsigned generation_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;
};

struct parallel_options {
    // 0 = automatic: 4 below 1e7, 12 at/above 1e7, capped by hardware.
    unsigned threads = 0;
    std::size_t min_n = 2000000;
    std::size_t max_buckets = 65536;
    // 0 = n * sizeof(T).
    std::size_t scratch_budget = 0;
    // Optional persistent pool; empty means a temporary pool per call.
    thread_pool* pool = nullptr;
};

namespace parallel_detail {

inline unsigned choose_threads(std::size_t n) {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 4;
    if (n < 10000000) return (hw > 4) ? 4 : hw;
    return (hw > 12) ? 12 : hw;
}

template <class T, class KeyFn, class LessFn, class Fallback>
void run(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
         const options& opt, const parallel_options& popt, Fallback fallback) {
    if (n < 2) return;

    unsigned threads = popt.threads ? popt.threads : choose_threads(n);
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw != 0 && threads > hw) threads = hw;
    if (popt.pool && threads > popt.pool->size()) threads = popt.pool->size();
    if (threads > n) threads = static_cast<unsigned>(n);

    if (threads <= 1 || n < popt.min_n) {
        detail::sort_impl(data, n, key, less, opt, fallback);
        return;
    }
    if (!detail::prepare(data, n, key, less, opt, fallback)) return;

    std::unique_ptr<thread_pool> local_pool;
    thread_pool* pool = popt.pool;
    if (!pool) {
        local_pool = std::make_unique<thread_pool>(threads);
        pool = local_pool.get();
        threads = pool->size();
    }

    std::size_t buckets = opt.bucket_limit ? opt.bucket_limit : 1;
    if (popt.max_buckets) buckets = (std::min)(buckets, popt.max_buckets);
    buckets = (std::max)(std::size_t(1), (std::min)(n, buckets));

    std::vector<std::uint64_t> minima(threads);
    std::vector<std::uint64_t> maxima(threads);
    pool->run(threads, [&](unsigned t) {
        const std::size_t lo = n * t / threads;
        const std::size_t hi = n * (t + 1) / threads;
        std::uint64_t mn = key(data[lo]);
        std::uint64_t mx = mn;
        for (std::size_t i = lo + 1; i < hi; ++i) {
            const std::uint64_t k = key(data[i]);
            if (k < mn) mn = k;
            if (k > mx) mx = k;
        }
        minima[t] = mn;
        maxima[t] = mx;
    });

    std::uint64_t minimum = minima[0];
    std::uint64_t maximum = maxima[0];
    for (unsigned t = 1; t < threads; ++t) {
        minimum = (std::min)(minimum, minima[t]);
        maximum = (std::max)(maximum, maxima[t]);
    }
    if (minimum == maximum) return;

    const double span = static_cast<double>(maximum - minimum);
    const double scale = static_cast<double>(buckets) / span;
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        if (k <= minimum) return 0;
        if (k >= maximum) return buckets - 1;
        const std::size_t b =
            static_cast<std::size_t>(static_cast<double>(k - minimum) * scale);
        return b >= buckets ? buckets - 1 : b;
    };

    const bool use_cache = opt.cache_bucket_index && buckets <= 65536;
    std::vector<std::uint16_t> cache;
    if (use_cache) cache.resize(n);

    std::vector<std::vector<std::uint32_t>> local(
        threads, std::vector<std::uint32_t>(buckets, 0));
    pool->run(threads, [&](unsigned t) {
        const std::size_t lo = n * t / threads;
        const std::size_t hi = n * (t + 1) / threads;
        auto& counts = local[t];
        if (use_cache) {
            for (std::size_t i = lo; i < hi; ++i) {
                const std::size_t b = bucket_of(key(data[i]));
                cache[i] = static_cast<std::uint16_t>(b);
                ++counts[b];
            }
        } else {
            for (std::size_t i = lo; i < hi; ++i) {
                ++counts[bucket_of(key(data[i]))];
            }
        }
    });

    std::vector<std::uint32_t> total(buckets, 0);
    for (unsigned t = 0; t < threads; ++t) {
        for (std::size_t b = 0; b < buckets; ++b) total[b] += local[t][b];
    }

    std::vector<std::uint32_t> start(buckets, 0);
    std::vector<std::uint32_t> finish(buckets, 0);
    std::uint32_t sum = 0;
    std::size_t largest_group = 0;
    for (std::size_t b = 0; b < buckets; ++b) {
        start[b] = sum;
        sum += total[b];
        finish[b] = sum;
        if (total[b] > largest_group) {
            largest_group = static_cast<std::size_t>(total[b]);
        }
    }

    std::vector<std::vector<std::uint32_t>> offsets(
        threads, std::vector<std::uint32_t>(buckets, 0));
    std::vector<std::uint32_t> cursor = start;
    for (unsigned t = 0; t < threads; ++t) {
        for (std::size_t b = 0; b < buckets; ++b) {
            offsets[t][b] = cursor[b];
            cursor[b] += local[t][b];
        }
    }
    local.clear();
    local.shrink_to_fit();

    std::vector<T> buffer(n);
    pool->run(threads, [&](unsigned t) {
        const std::size_t lo = n * t / threads;
        const std::size_t hi = n * (t + 1) / threads;
        auto& off = offsets[t];
        if (use_cache) {
            for (std::size_t i = lo; i < hi; ++i) {
                const std::size_t b = cache[i];
                buffer[off[b]++] = data[i];
            }
        } else {
            for (std::size_t i = lo; i < hi; ++i) {
                const std::size_t b = bucket_of(key(data[i]));
                buffer[off[b]++] = data[i];
            }
        }
    });

    pool->run(threads, [&](unsigned t) {
        const std::size_t lo = n * t / threads;
        const std::size_t hi = n * (t + 1) / threads;
        std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(lo),
                  buffer.begin() + static_cast<std::ptrdiff_t>(hi), data + lo);
    });

    auto sort_group = [&](std::size_t b, std::vector<T>& scratch) {
        const std::size_t len =
            static_cast<std::size_t>(finish[b] - start[b]);
        if (len < 2) return;
        T* group = data + start[b];
        if (len < opt.leaf_sort) {
            if (len == 2) {
                detail::sort2_less(group, less);
            } else if (len == 3) {
                detail::sort3_less(group, less);
            } else {
                detail::budgeted_insertion(group, len, less, len * len);
            }
        } else if (len < opt.small_sort) {
            fallback(group, group + len, less);
        } else {
            detail::sort_range(group, len, 1, key, less, fallback, opt, scratch);
        }
    };

    std::size_t group_threads = threads;
    if (largest_group > 0) {
        const std::size_t budget =
            popt.scratch_budget ? popt.scratch_budget : n * sizeof(T);
        const std::size_t fit = budget / (largest_group * sizeof(T));
        if (fit < group_threads) {
            group_threads = fit ? static_cast<unsigned>(fit) : 1;
        }
    }

    if (group_threads <= 1) {
        std::vector<T> scratch(largest_group);
        for (std::size_t b = 0; b < buckets; ++b) {
            sort_group(b, scratch);
        }
    } else {
        std::atomic<std::size_t> next{0};
        pool->run(group_threads, [&](unsigned) {
            std::vector<T> scratch(largest_group);
            for (;;) {
                const std::size_t b =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (b >= buckets) break;
                sort_group(b, scratch);
            }
        });
    }
}

}  // namespace parallel_detail

template <class T, class Fallback = default_fallback>
void parallel_sort(T* data, std::size_t n,
                   const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& value) { return detail::key_of(value); };
    if constexpr (std::is_same<T, double>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_double(a, b);
        };
        parallel_detail::run(data, n, key, less, opt, popt, fallback);
    } else if constexpr (std::is_same<T, float>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_float(a, b);
        };
        parallel_detail::run(data, n, key, less, opt, popt, fallback);
    } else {
        auto less = [&key](const T& a, const T& b) {
            return key(a) < key(b);
        };
        parallel_detail::run(data, n, key, less, opt, popt, fallback);
    }
}

template <class T, class Fallback = default_fallback>
void parallel_sort(std::vector<T>& values, const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    parallel_sort(values.data(), values.size(), opt, popt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(T* data, std::size_t n, KeyFn user_key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    auto key = [&user_key](const T& value) -> std::uint64_t {
        return static_cast<std::uint64_t>(user_key(value));
    };
    auto less = [&key](const T& a, const T& b) { return key(a) < key(b); };
    parallel_detail::run(data, n, key, less, opt, popt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(std::vector<T>& values, KeyFn key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    parallel_sort_by(values.data(), values.size(), key, opt, popt, fallback);
}

}  // namespace oblivone

#endif  // OBLIVONE_PARALLEL_HPP

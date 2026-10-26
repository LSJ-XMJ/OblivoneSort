// OblivoneSort parallel API (final header).
//
// This header is optional: the core <oblivone/sort.hpp> stays free of
// <thread>/<mutex> dependencies. Include this header only when parallel
// sorting is wanted.
//
// Thread safety:
//   * different parallel_sort calls may run concurrently;
//   * one thread_pool serializes its own run() calls;
//   * a user-provided pool must outlive the parallel_sort call.

#ifndef OBLIVONE_PARALLEL_HPP
#define OBLIVONE_PARALLEL_HPP

#include "sort.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace oblivone {

// A small fixed-size thread pool.  run() submits exactly `tasks` jobs and
// waits for all of them.  Exceptions from jobs are rethrown by run().
class thread_pool {
public:
    explicit thread_pool(unsigned threads = 0) {
        n_ = threads ? threads : std::thread::hardware_concurrency();
        if (n_ == 0) n_ = 1;
        workers_.reserve(n_);
        for (unsigned i = 0; i < n_; ++i) {
            workers_.emplace_back([this] { worker(); });
        }
    }

    ~thread_pool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    unsigned size() const { return n_; }

    template <class F>
    void run(unsigned tasks, F fn) {
        std::unique_lock<std::mutex> run_lock(run_mutex_);
        if (tasks == 0) return;

        {
            std::lock_guard<std::mutex> lk(m_);
            jobs_.clear();
            jobs_.reserve(tasks);
            for (unsigned i = 0; i < tasks; ++i) {
                jobs_.emplace_back([this, i, &fn] { fn(i); });
            }
            next_ = 0;
            remaining_ = tasks;
            error_ = nullptr;
        }
        cv_.notify_all();

        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this] { return remaining_ == 0; });
        auto err = error_;
        error_ = nullptr;
        lk.unlock();
        if (err) std::rethrow_exception(err);
    }

private:
    void worker() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || next_ < jobs_.size(); });
                if (stop_ && next_ >= jobs_.size()) return;
                job = std::move(jobs_[next_++]);
            }
            try {
                job();
            } catch (...) {
                std::lock_guard<std::mutex> lk(m_);
                if (!error_) error_ = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> lk(m_);
                if (--remaining_ == 0) done_.notify_all();
            }
        }
    }

    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::mutex run_mutex_;
    std::condition_variable cv_;
    std::condition_variable done_;
    std::vector<std::function<void()>> jobs_;
    std::size_t next_ = 0;
    std::size_t remaining_ = 0;
    std::exception_ptr error_;
    bool stop_ = false;
};

struct parallel_options {
    // 0 = automatic: <=4 threads below 1e7, <=12 at 1e7 and above.
    unsigned threads = 0;
    // Below this size, use the serial dispatcher.
    // 2e6 is the safe default: 1e6 few/organ can regress under parallel.
    std::size_t min_n = 2000000;
    // Per-thread bucket tables are capped to keep T * B memory bounded.
    // 0 = no extra cap (not recommended).
    std::size_t max_buckets = 65536;
    // Optional user-owned pool.  If null, a temporary pool is created.
    thread_pool* pool = nullptr;
};

namespace parallel_detail {

inline unsigned auto_threads(std::size_t n) {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 4;
    if (n < 10000000) return (hw > 4) ? 4 : hw;
    return (hw > 12) ? 12 : hw;
}

template <class T, class KeyFn, class LessFn, class Fallback>
void impl(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
          bool native_less, const options& opt,
          const parallel_options& popt, Fallback fallback) {
    if (n < 2) return;

    unsigned nthreads = popt.threads ? popt.threads : auto_threads(n);
    if (popt.pool) {
        const unsigned cap = popt.pool->size();
        if (nthreads == 0 || nthreads > cap) nthreads = cap;
    }
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

    // Same early exits as the serial dispatcher.
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

    std::unique_ptr<thread_pool> local_pool;
    thread_pool* pool = popt.pool;
    if (!pool) {
        local_pool = std::make_unique<thread_pool>(nthreads);
        pool = local_pool.get();
        nthreads = pool->size();
    }

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
    std::size_t cap = (std::min)(opt.max_buckets, auto_cap);
    if (popt.max_buckets != 0) cap = (std::min)(cap, popt.max_buckets);
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(n, cap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double x = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(x);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::vector<std::uint32_t>> local(
        nthreads, std::vector<std::uint32_t>(B, 0));
    pool->run(nthreads, [&](unsigned t) {
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
    pool->run(nthreads, [&](unsigned t) {
        const std::size_t lo = n * t / nthreads;
        const std::size_t hi = n * (t + 1) / nthreads;
        auto& o = off[t];
        for (std::size_t i = lo; i < hi; ++i) {
            const std::size_t b = bucket_of(key(data[i]));
            buf[o[b]++] = data[i];
        }
    });
    pool->run(nthreads, [&](unsigned t) {
        const std::size_t lo = n * t / nthreads;
        const std::size_t hi = n * (t + 1) / nthreads;
        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(lo),
                  buf.begin() + static_cast<std::ptrdiff_t>(hi), data + lo);
    });

    std::atomic<std::size_t> next{0};
    pool->run(nthreads, [&](unsigned) {
        std::vector<T> scratch(max_group);
        for (;;) {
            const std::size_t b = next.fetch_add(1, std::memory_order_relaxed);
            if (b >= B) break;
            const std::uint32_t len = end[b] - start[b];
            if (len > 1) {
                if (len < opt.leaf_sort) {
                    if (native_less) {
                        detail::insertion_sort_less(data + start[b], len, less);
                    } else {
                        detail::insertion_sort(data + start[b], len, key);
                    }
                } else {
                    detail::sort_range(data + start[b], len, 1, key, less,
                                       native_less, fallback, opt, scratch);
                }
            }
        }
    });
}

}  // namespace parallel_detail

template <class T, class Fallback = default_fallback>
void parallel_sort(T* data, std::size_t n,
                   const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& v) { return detail::to_key(v); };
    if constexpr (std::is_same<T, double>::value) {
        parallel_detail::impl(data, n, key, detail::less_double, true, opt, popt,
                              fallback);
    } else {
        auto less = [&](const T& x, const T& y) { return key(x) < key(y); };
        parallel_detail::impl(data, n, key, less, false, opt, popt, fallback);
    }
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(T* data, std::size_t n, KeyFn key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    auto less = [&](const T& x, const T& y) { return key(x) < key(y); };
    parallel_detail::impl(data, n, key, less, false, opt, popt, fallback);
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

}  // namespace oblivone

#endif  // OBLIVONE_PARALLEL_HPP

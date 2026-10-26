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

// 固定大小线程池。run() 提交 tasks 个任务并等待全部完成。
// 同一个池的 run() 会串行化；不同池可以并发。
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
        wake_.notify_all();
        for (auto& worker : workers_) worker.join();
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    unsigned size() const { return n_; }

    template <class Fn>
    void run(unsigned tasks, Fn fn) {
        std::unique_lock<std::mutex> run_lock(run_mutex_);
        if (tasks == 0) return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.clear();
            jobs_.reserve(tasks);
            for (unsigned i = 0; i < tasks; ++i) {
                jobs_.emplace_back([this, i, &fn] { fn(i); });
            }
            next_ = 0;
            remaining_ = tasks;
            error_ = nullptr;
        }
        wake_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        finished_.wait(lock, [this] { return remaining_ == 0; });
        auto error = error_;
        error_ = nullptr;
        lock.unlock();
        if (error) std::rethrow_exception(error);
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] {
                    return stop_ || next_ < jobs_.size();
                });
                if (stop_ && next_ >= jobs_.size()) return;
                job = std::move(jobs_[next_++]);
            }

            try {
                job();
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!error_) error_ = std::current_exception();
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--remaining_ == 0) finished_.notify_all();
            }
        }
    }

    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::mutex run_mutex_;
    std::condition_variable wake_;
    std::condition_variable finished_;
    std::vector<std::function<void()>> jobs_;
    std::size_t next_ = 0;
    std::size_t remaining_ = 0;
    std::exception_ptr error_;
    bool stop_ = false;
};

struct parallel_options {
    // 0 表示自动：n<1e7 用 4，n>=1e7 用 12，受 hardware 限制。
    unsigned threads = 0;

    // 小于此值直接走串行 dispatcher。
    std::size_t min_n = 2000000;

    // 并行每线程计数表上限；0 表示不额外限制。
    std::size_t max_buckets = 65536;

    // 组排序 scratch 字节预算；0 表示 n * sizeof(T)。
    std::size_t scratch_budget = 0;

    // 可选用户线程池；为空时每次调用创建临时池。
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
          const options& opt, const parallel_options& popt, Fallback fallback) {
    if (n < 2) return;

    unsigned nthreads = popt.threads ? popt.threads : auto_threads(n);
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware != 0 && nthreads > hardware) nthreads = hardware;
    if (popt.pool) {
        const unsigned limit = popt.pool->size();
        if (nthreads > limit) nthreads = limit;
    }

    if (nthreads <= 1 || n < popt.min_n) {
        detail::sort_impl(data, n, key, less, opt, fallback);
        return;
    }

    if (!detail::prepare(data, n, key, less, opt, fallback)) return;

    std::unique_ptr<thread_pool> local_pool;
    thread_pool* pool = popt.pool;
    if (!pool) {
        local_pool = std::make_unique<thread_pool>(nthreads);
        pool = local_pool.get();
        nthreads = pool->size();
    }
    if (nthreads <= 1) {
        std::vector<T> buf(n);
        detail::sort_range(data, n, 0, key, less, fallback, opt, buf);
        return;
    }

    std::size_t cap = opt.bucket_limit ? opt.bucket_limit : 1;
    if (popt.max_buckets != 0) cap = (std::min)(cap, popt.max_buckets);
    const std::size_t buckets =
        (std::max)(std::size_t(1), (std::min)(n, cap));

    std::uint64_t minimum = key(data[0]);
    std::uint64_t maximum = minimum;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = key(data[i]);
        if (k < minimum) minimum = k;
        if (k > maximum) maximum = k;
    }
    if (minimum == maximum) return;

    const std::uint64_t range = maximum - minimum;
    const double scale =
        static_cast<double>(buckets) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        if (k <= minimum) return 0;
        if (k >= maximum) return buckets - 1;
        const double pos = static_cast<double>(k - minimum) * scale;
        const std::size_t b = static_cast<std::size_t>(pos);
        return b >= buckets ? buckets - 1 : b;
    };

    std::vector<std::vector<std::uint32_t>> local(
        nthreads, std::vector<std::uint32_t>(buckets, 0));
    pool->run(nthreads, [&](unsigned t) {
        const std::size_t begin = n * t / nthreads;
        const std::size_t end = n * (t + 1) / nthreads;
        auto& counts = local[t];
        for (std::size_t i = begin; i < end; ++i) {
            ++counts[bucket_of(key(data[i]))];
        }
    });

    std::vector<std::uint32_t> total(buckets, 0);
    for (unsigned t = 0; t < nthreads; ++t) {
        for (std::size_t b = 0; b < buckets; ++b) {
            total[b] += local[t][b];
        }
    }

    std::vector<std::uint32_t> start(buckets, 0);
    std::vector<std::uint32_t> end(buckets, 0);
    std::uint32_t sum = 0;
    std::size_t max_group = 0;
    for (std::size_t b = 0; b < buckets; ++b) {
        start[b] = sum;
        sum += total[b];
        end[b] = sum;
        if (total[b] > max_group) max_group = total[b];
    }

    std::vector<std::vector<std::uint32_t>> offset(
        nthreads, std::vector<std::uint32_t>(buckets, 0));
    std::vector<std::uint32_t> cursor = start;
    for (unsigned t = 0; t < nthreads; ++t) {
        for (std::size_t b = 0; b < buckets; ++b) {
            offset[t][b] = cursor[b];
            cursor[b] += local[t][b];
        }
    }
    local.clear();
    local.shrink_to_fit();

    std::vector<T> buf(n);
    pool->run(nthreads, [&](unsigned t) {
        const std::size_t begin = n * t / nthreads;
        const std::size_t end_pos = n * (t + 1) / nthreads;
        auto& off = offset[t];
        for (std::size_t i = begin; i < end_pos; ++i) {
            const std::size_t b = bucket_of(key(data[i]));
            buf[off[b]++] = data[i];
        }
    });
    pool->run(nthreads, [&](unsigned t) {
        const std::size_t begin = n * t / nthreads;
        const std::size_t end_pos = n * (t + 1) / nthreads;
        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(begin),
                  buf.begin() + static_cast<std::ptrdiff_t>(end_pos),
                  data + begin);
    });

    auto process_group = [&](std::size_t b, std::vector<T>& scratch) {
        const std::size_t len =
            static_cast<std::size_t>(end[b] - start[b]);
        if (len < 2) return;
        T* group = data + start[b];
        if (len < opt.leaf_sort) {
            if (len == 2) {
                detail::sort2_less(group, less);
            } else if (len == 3) {
                detail::sort3_less(group, less);
            } else {
                detail::insertion_sort_less(group, len, less);
            }
        } else if (len < opt.small_sort) {
            fallback(group, group + len, less);
        } else {
            detail::sort_range(group, len, 1, key, less, fallback, opt,
                               scratch);
        }
    };

    const std::size_t budget =
        popt.scratch_budget ? popt.scratch_budget : n * sizeof(T);
    unsigned group_threads = nthreads;
    if (max_group > 0) {
        const std::size_t per_task = max_group * sizeof(T);
        const std::size_t fit = per_task ? budget / per_task : 0;
        if (fit < group_threads) {
            group_threads = fit ? static_cast<unsigned>(fit) : 1;
        }
    }

    if (group_threads <= 1) {
        std::vector<T> scratch(max_group);
        for (std::size_t b = 0; b < buckets; ++b) {
            process_group(b, scratch);
        }
    } else {
        std::atomic<std::size_t> next{0};
        pool->run(group_threads, [&](unsigned) {
            std::vector<T> scratch(max_group);
            for (;;) {
                const std::size_t b =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (b >= buckets) break;
                process_group(b, scratch);
            }
        });
    }
}

}  // namespace parallel_detail

template <class T, class Fallback = default_fallback>
void parallel_sort(T* data, std::size_t n, const options& opt = {},
                   const parallel_options& popt = {},
                   Fallback fallback = Fallback{}) {
    auto key = [](const T& value) { return detail::key_of(value); };
    if constexpr (std::is_same<T, double>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_double(a, b);
        };
        parallel_detail::impl(data, n, key, less, opt, popt, fallback);
    } else if constexpr (std::is_same<T, float>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_float(a, b);
        };
        parallel_detail::impl(data, n, key, less, opt, popt, fallback);
    } else {
        auto less = [&key](const T& a, const T& b) {
            return key(a) < key(b);
        };
        parallel_detail::impl(data, n, key, less, opt, popt, fallback);
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
    auto less = [&key](const T& a, const T& b) {
        return key(a) < key(b);
    };
    parallel_detail::impl(data, n, key, less, opt, popt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void parallel_sort_by(std::vector<T>& values, KeyFn user_key,
                      const options& opt = {},
                      const parallel_options& popt = {},
                      Fallback fallback = Fallback{}) {
    parallel_sort_by(values.data(), values.size(), user_key, opt, popt,
                     fallback);
}

}  // namespace oblivone

#endif  // OBLIVONE_PARALLEL_HPP

#ifndef OBLIVONE_ADVANCED_ENGINE_HPP
#define OBLIVONE_ADVANCED_ENGINE_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace oblivone {
namespace advanced {

// Work-stealing task engine.
//
// Lanes:
//   0 = outer partition
//   1 = inner partition
//   2 = group work
//
// A task must never block waiting for another task. Continuations are submitted
// as new tasks. The engine can be reused for multiple phases.
class engine {
public:
    using task_fn = std::function<void()>;

    explicit engine(unsigned workers)
        : workers_(workers ? workers : 1), queues_(workers_) {}

    engine(const engine&) = delete;
    engine& operator=(const engine&) = delete;

    ~engine() { stop(); }

    unsigned size() const { return workers_; }

    std::size_t active() const { return active_.load(); }

    std::size_t idle() const {
        const std::size_t a = active_.load();
        return a >= workers_ ? 0 : static_cast<std::size_t>(workers_) - a;
    }

    // Team available to a child partition created by an active worker.
    std::size_t team_for_child() const {
        const std::size_t target = idle() + 1;
        return (std::max)(std::size_t(1),
                          (std::min)(static_cast<std::size_t>(workers_),
                                     target));
    }

    void reserve(std::size_t n) {
        const std::size_t cap =
            workers_ ? static_cast<std::size_t>(workers_ - 1) : 0;
        reserved_.store((std::min)(n, cap));
    }

    void set_node_limit(std::size_t n) { node_limit_.store(n); }

    void set_memory_limit(std::size_t bytes) {
        memory_limit_.store(bytes ? bytes
                                  : (std::numeric_limits<std::size_t>::max)());
    }

    bool try_node(std::size_t bytes) {
        const std::size_t node_limit = node_limit_.load();
        std::size_t nodes = nodes_.load();
        for (;;) {
            if (nodes >= node_limit) return false;
            if (nodes_.compare_exchange_weak(nodes, nodes + 1,
                                             std::memory_order_acq_rel)) {
                break;
            }
        }

        const std::size_t memory_limit = memory_limit_.load();
        std::size_t used = memory_.load();
        for (;;) {
            if (bytes > memory_limit || used > memory_limit - bytes) {
                nodes_.fetch_sub(1, std::memory_order_acq_rel);
                return false;
            }
            if (memory_.compare_exchange_weak(used, used + bytes,
                                              std::memory_order_acq_rel)) {
                return true;
            }
        }
    }

    void release_node(std::size_t bytes) {
        memory_.fetch_sub(bytes, std::memory_order_acq_rel);
        nodes_.fetch_sub(1, std::memory_order_acq_rel);
    }

    // lane: 0 outer, 1 inner, 2 group. owner < 0 selects round-robin.
    void submit(task_fn fn, int lane = 0, int owner = -1) {
        if (!fn) return;
        const unsigned home =
            owner >= 0
                ? static_cast<unsigned>(owner) % workers_
                : next_home_.fetch_add(1, std::memory_order_relaxed) % workers_;
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(queues_[home].mutex);
            const int clamped = (std::max)(0, (std::min)(2, lane));
            queues_[home].lanes[clamped].push_back(std::move(fn));
        }
        {
            std::lock_guard<std::mutex> lock(wait_mutex_);
        }
        wake_.notify_one();
    }

    void start() {
        if (started_) return;
        started_ = true;
        threads_.reserve(workers_);
        for (unsigned i = 0; i < workers_; ++i) {
            threads_.emplace_back([this, i] { worker_loop(i); });
        }
        wake_.notify_all();
    }

    // Wait for the current phase; the engine remains reusable.
    void wait() {
        {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            done_cv_.wait(lock, [this] { return pending_.load() == 0; });
        }
        std::exception_ptr error;
        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            error = error_;
        }
        if (error) {
            stop();
            std::rethrow_exception(error);
        }
    }

    void stop() {
        if (stopping_.exchange(true)) {
            join();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(wait_mutex_);
        }
        wake_.notify_all();
        join();
    }

private:
    struct worker_queue {
        std::mutex mutex;
        std::deque<task_fn> lanes[3];
    };

    static thread_local int current_worker_;

    bool take_local(unsigned id, int lane, task_fn& fn) {
        auto& q = queues_[id];
        std::lock_guard<std::mutex> lock(q.mutex);
        if (q.lanes[lane].empty()) return false;
        fn = std::move(q.lanes[lane].back());
        q.lanes[lane].pop_back();
        return true;
    }

    bool steal(unsigned id, int lane, task_fn& fn) {
        for (unsigned k = 1; k < workers_; ++k) {
            auto& q = queues_[(id + k) % workers_];
            std::lock_guard<std::mutex> lock(q.mutex);
            if (!q.lanes[lane].empty()) {
                fn = std::move(q.lanes[lane].front());
                q.lanes[lane].pop_front();
                return true;
            }
        }
        return false;
    }

    bool has_lane(int lane) {
        for (unsigned i = 0; i < workers_; ++i) {
            std::lock_guard<std::mutex> lock(queues_[i].mutex);
            if (!queues_[i].lanes[lane].empty()) return true;
        }
        return false;
    }

    bool has_any() {
        for (unsigned i = 0; i < workers_; ++i) {
            std::lock_guard<std::mutex> lock(queues_[i].mutex);
            for (int lane = 0; lane < 3; ++lane) {
                if (!queues_[i].lanes[lane].empty()) return true;
            }
        }
        return false;
    }

    void join() {
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

    void worker_loop(unsigned id) {
        current_worker_ = static_cast<int>(id);
        for (;;) {
            task_fn fn;
            bool got = take_local(id, 0, fn) || steal(id, 0, fn);
            if (!got) {
                const std::size_t a = active_.load();
                if (a + reserved_.load() < workers_) {
                    got = take_local(id, 1, fn) || steal(id, 1, fn);
                }
            }
            if (!got) {
                const std::size_t a = active_.load();
                if (a + reserved_.load() < workers_) {
                    got = take_local(id, 2, fn) || steal(id, 2, fn);
                }
            }
            if (!got) {
                std::unique_lock<std::mutex> lock(wait_mutex_);
                wake_.wait(lock, [this] {
                    return stopping_.load() || has_any();
                });
                if (stopping_.load()) return;
                continue;
            }

            try {
                active_.fetch_add(1, std::memory_order_relaxed);
                fn();
                active_.fetch_sub(1, std::memory_order_relaxed);
            } catch (...) {
                active_.fetch_sub(1, std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    if (!error_) error_ = std::current_exception();
                }
                pending_.store(0, std::memory_order_relaxed);
                stopping_.store(true, std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(wait_mutex_);
                }
                done_cv_.notify_all();
                wake_.notify_all();
                return;
            }

            // A finished task may make reserved capacity available for a
            // sleeping worker holding only lane 1/2 work.
            wake_.notify_all();
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                {
                    std::lock_guard<std::mutex> lock(wait_mutex_);
                }
                done_cv_.notify_all();
            }
        }
    }

    unsigned workers_ = 1;
    std::vector<worker_queue> queues_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> next_home_{0};
    std::atomic<std::size_t> pending_{0};
    std::atomic<std::size_t> active_{0};
    std::atomic<std::size_t> reserved_{0};
    std::atomic<std::size_t> nodes_{0};
    std::atomic<std::size_t> node_limit_{(std::numeric_limits<std::size_t>::max)()};
    std::atomic<std::size_t> memory_{0};
    std::atomic<std::size_t> memory_limit_{(std::numeric_limits<std::size_t>::max)()};
    std::atomic<bool> stopping_{false};
    std::mutex wait_mutex_;
    std::mutex error_mutex_;
    std::condition_variable done_cv_;
    std::condition_variable wake_;
    bool started_ = false;
    std::exception_ptr error_;
};

inline thread_local int engine::current_worker_ = -1;

}  // namespace advanced
}  // namespace oblivone

#endif  // OBLIVONE_ADVANCED_ENGINE_HPP

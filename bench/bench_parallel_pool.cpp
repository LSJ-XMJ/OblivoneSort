// Persistent thread-pool parallel root + depth-1 groups.
// Usage: bench_parallel_pool [n] [reps] [Bcap]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t keyd(double x) { return oblivone::key_of(x); }
bool less_d(double a, double b) { return keyd(a) < keyd(b); }

class Pool {
public:
    explicit Pool(unsigned n) {
        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            workers_.emplace_back([this] { worker(); });
        }
    }
    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    void run(unsigned tasks, const std::function<void(unsigned)>& fn) {
        {
            std::lock_guard<std::mutex> lk(m_);
            jobs_.clear();
            jobs_.reserve(tasks);
            for (unsigned i = 0; i < tasks; ++i) {
                jobs_.emplace_back([this, i, &fn] { fn(i); });
            }
            next_ = 0;
            remaining_ = tasks;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this] { return remaining_ == 0; });
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
            job();
            {
                std::lock_guard<std::mutex> lk(m_);
                if (--remaining_ == 0) done_.notify_all();
            }
        }
    }
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_;
    std::condition_variable done_;
    std::vector<std::function<void()>> jobs_;
    std::size_t next_ = 0;
    std::size_t remaining_ = 0;
    bool stop_ = false;
};

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(12000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (p == 1) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (p == 2) {
        std::sort(v.begin(), v.end());
        std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    }
    return v;
}

void pool_sort(double* a, std::size_t n, Pool& pool, unsigned T,
               std::size_t Bcap) {
    if (n < 2) return;
    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(n, Bcap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double x = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(x);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::vector<std::uint32_t>> local(
        T, std::vector<std::uint32_t>(B, 0));
    pool.run(T, [&](unsigned t) {
        const std::size_t lo = n * t / T;
        const std::size_t hi = n * (t + 1) / T;
        auto& cnt = local[t];
        for (std::size_t i = lo; i < hi; ++i) ++cnt[bucket_of(keyd(a[i]))];
    });

    std::vector<std::uint32_t> total(B, 0);
    for (unsigned t = 0; t < T; ++t) {
        for (std::size_t b = 0; b < B; ++b) total[b] += local[t][b];
    }
    std::vector<std::uint32_t> start(B, 0), end(B, 0);
    std::uint32_t running = 0;
    std::size_t max_group = 0;
    for (std::size_t b = 0; b < B; ++b) {
        start[b] = running;
        running += total[b];
        end[b] = running;
        if (total[b] > max_group) max_group = total[b];
    }

    std::vector<std::vector<std::uint32_t>> off(
        T, std::vector<std::uint32_t>(B, 0));
    std::vector<std::uint32_t> cur = start;
    for (unsigned t = 0; t < T; ++t) {
        for (std::size_t b = 0; b < B; ++b) {
            off[t][b] = cur[b];
            cur[b] += local[t][b];
        }
    }
    local.clear();
    local.shrink_to_fit();

    std::vector<double> buf(n);
    pool.run(T, [&](unsigned t) {
        const std::size_t lo = n * t / T;
        const std::size_t hi = n * (t + 1) / T;
        auto& o = off[t];
        for (std::size_t i = lo; i < hi; ++i) {
            const std::size_t b = bucket_of(keyd(a[i]));
            buf[o[b]++] = a[i];
        }
    });
    pool.run(T, [&](unsigned t) {
        const std::size_t lo = n * t / T;
        const std::size_t hi = n * (t + 1) / T;
        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(lo),
                  buf.begin() + static_cast<std::ptrdiff_t>(hi), a + lo);
    });

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    const auto less = [](const double& x, const double& y) { return less_d(x, y); };
    oblivone::options opt;
    oblivone::default_fallback fb;

    std::atomic<std::size_t> next{0};
    pool.run(T, [&](unsigned) {
        std::vector<double> scratch(max_group);
        for (;;) {
            const std::size_t b = next.fetch_add(1, std::memory_order_relaxed);
            if (b >= B) break;
            const std::uint32_t len = end[b] - start[b];
            if (len < 2) continue;
            if (len < opt.leaf_sort) {
                for (std::uint32_t i = start[b] + 1; i < end[b]; ++i) {
                    double v = a[i];
                    const std::uint64_t kv = keyd(v);
                    std::uint32_t j = i;
                    while (j > start[b] && kv < keyd(a[j - 1])) {
                        a[j] = a[j - 1];
                        --j;
                    }
                    a[j] = v;
                }
            } else {
                oblivone::detail::sort_range(a + start[b], len, 1, key, less,
                                             false, fb, opt, scratch);
            }
        }
    });
}

template <class F>
double median_ms(const std::vector<double>& base, F f, int reps) {
    std::vector<double> times;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t Bcap =
        (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                   : 65536;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    std::printf("pattern,n,T,base_ms,pool_ms,ratio\n");
    const char* names[] = {"random", "few", "organ"};
    const unsigned Ts[] = {2, 4, 8};

    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);
        const auto base_run = [&](std::vector<double>& v) {
            oblivone::options opt;
            opt.max_buckets = Bcap;
            opt.bucket_cap = Bcap;
            oblivone::oblivone_sort(v.data(), v.size(), key, opt);
        };
        auto check = data;
        base_run(check);
        if (!std::is_sorted(check.begin(), check.end(), less_d)) {
            std::printf("BASE MISMATCH\n");
            return 2;
        }
        const double tb = median_ms(data, base_run, reps);

        for (const unsigned T : Ts) {
            Pool pool(T);
            auto chk = data;
            pool_sort(chk.data(), chk.size(), pool, T, Bcap);
            if (!std::is_sorted(chk.begin(), chk.end(), less_d)) {
                std::printf("T=%u MISMATCH\n", T);
                return 2;
            }
            const double tp = median_ms(data, [&](std::vector<double>& v) {
                pool_sort(v.data(), v.size(), pool, T, Bcap);
            }, reps);
            std::printf("%s,%zu,%u,%.3f,%.3f,%.3f\n", names[p], n, T, tb, tp,
                        tp / tb);
        }
    }
    return 0;
}

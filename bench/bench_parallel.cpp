// Parallel root-level count + scatter; recursive groups stay serial.
// Usage: bench_parallel [n] [reps] [Bcap]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t keyd(double x) { return oblivone::key_of(x); }
bool less_d(double a, double b) { return keyd(a) < keyd(b); }

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(11000 + static_cast<std::uint64_t>(p));
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

void parallel_root(double* a, std::size_t n, unsigned T, std::size_t Bcap,
                   bool parallel_groups = false) {
    if (n < 2) return;
    if (T <= 1) {
        const auto key = [](const double& x) { return oblivone::key_of(x); };
        oblivone::options opt;
        opt.max_buckets = Bcap;
        opt.bucket_cap = Bcap;
        oblivone::oblivone_sort(a, n, key, opt);
        return;
    }

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
    std::vector<std::thread> threads;
    threads.reserve(T);
    for (unsigned t = 0; t < T; ++t) {
        threads.emplace_back([&, t] {
            const std::size_t lo = n * t / T;
            const std::size_t hi = n * (t + 1) / T;
            auto& cnt = local[t];
            for (std::size_t i = lo; i < hi; ++i) {
                ++cnt[bucket_of(keyd(a[i]))];
            }
        });
    }
    for (auto& th : threads) th.join();
    threads.clear();

    std::vector<std::uint32_t> total(B, 0);
    for (unsigned t = 0; t < T; ++t) {
        for (std::size_t b = 0; b < B; ++b) total[b] += local[t][b];
    }
    std::vector<std::uint32_t> start(B, 0), end(B, 0);
    std::size_t max_group = 0;
    std::uint32_t running = 0;
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
    for (unsigned t = 0; t < T; ++t) {
        threads.emplace_back([&, t] {
            const std::size_t lo = n * t / T;
            const std::size_t hi = n * (t + 1) / T;
            auto& o = off[t];
            for (std::size_t i = lo; i < hi; ++i) {
                const std::size_t b = bucket_of(keyd(a[i]));
                buf[o[b]++] = a[i];
            }
        });
    }
    for (auto& th : threads) th.join();
    threads.clear();

    // parallel copy buf -> a
    for (unsigned t = 0; t < T; ++t) {
        threads.emplace_back([&, t] {
            const std::size_t lo = n * t / T;
            const std::size_t hi = n * (t + 1) / T;
            std::copy(buf.begin() + static_cast<std::ptrdiff_t>(lo),
                      buf.begin() + static_cast<std::ptrdiff_t>(hi),
                      a + lo);
        });
    }
    for (auto& th : threads) th.join();
    threads.clear();

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    const auto less = [](const double& x, const double& y) { return less_d(x, y); };
    oblivone::options opt;
    oblivone::default_fallback fb;

    auto process_group = [&](std::size_t b, std::vector<double>& scratch) {
        const std::uint32_t len = end[b] - start[b];
        if (len < 2) return;
        if (len < opt.leaf_sort) {
            if (len == 3) {
                std::uint64_t k0 = keyd(a[start[b]]);
                std::uint64_t k1 = keyd(a[start[b] + 1]);
                std::uint64_t k2 = keyd(a[start[b] + 2]);
                if (k1 < k0) { std::swap(a[start[b]], a[start[b] + 1]); std::swap(k0, k1); }
                if (k2 < k1) { std::swap(a[start[b] + 1], a[start[b] + 2]); std::swap(k1, k2); }
                if (k1 < k0) { std::swap(a[start[b]], a[start[b] + 1]); }
            } else {
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
            }
        } else {
            oblivone::detail::sort_range(a + start[b], len, 1, key, less, false,
                                         fb, opt, scratch);
        }
    };

    if (!parallel_groups) {
        for (std::size_t b = 0; b < B; ++b) process_group(b, buf);
    } else {
        std::atomic<std::size_t> next{0};
        for (unsigned t = 0; t < T; ++t) {
            threads.emplace_back([&] {
                std::vector<double> scratch(max_group);
                for (;;) {
                    const std::size_t b = next.fetch_add(1, std::memory_order_relaxed);
                    if (b >= B) break;
                    process_group(b, scratch);
                }
            });
        }
        for (auto& th : threads) th.join();
        threads.clear();
    }
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
                   : 10000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t Bcap =
        (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                   : 65536;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    std::printf("pattern,n,T,base_ms,root_ms,full_ms,root_ratio,full_ratio\n");
    const char* names[] = {"random", "few", "organ"};
    const unsigned Ts[] = {1, 2, 4, 8};

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
            if (T == 1) {
                std::printf("%s,%zu,%u,%.3f,%.3f,%.3f,1.000,1.000\n", names[p], n,
                            T, tb, tb, tb);
                continue;
            }
            auto chk_root = data;
            parallel_root(chk_root.data(), chk_root.size(), T, Bcap, false);
            auto chk_full = data;
            parallel_root(chk_full.data(), chk_full.size(), T, Bcap, true);
            if (!std::is_sorted(chk_root.begin(), chk_root.end(), less_d) ||
                !std::is_sorted(chk_full.begin(), chk_full.end(), less_d)) {
                std::printf("T=%u MISMATCH\n", T);
                return 2;
            }
            const double tr = median_ms(data, [&](std::vector<double>& v) {
                parallel_root(v.data(), v.size(), T, Bcap, false);
            }, reps);
            const double tf = median_ms(data, [&](std::vector<double>& v) {
                parallel_root(v.data(), v.size(), T, Bcap, true);
            }, reps);
            std::printf("%s,%zu,%u,%.3f,%.3f,%.3f,%.3f,%.3f\n", names[p], n, T,
                        tb, tr, tf, tr / tb, tf / tb);
        }
    }
    return 0;
}

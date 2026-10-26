// Prototype: Oblivone core with pure-double bucket mapping (no uint64 key).
// Compare against the current key-based core on random data.
//
// Usage: bench_double_map [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct opts {
    std::size_t small_sort = 16;
    std::size_t leaf_sort = 22;
    int max_depth = 16;
    std::size_t max_buckets = std::size_t(1) << 20;
};

struct stats {
    std::size_t nodes = 0;
    std::size_t sum_m = 0;
    std::size_t max_depth = 0;
    std::size_t fallback = 0;
};

stats g_stats;

bool less_d(double a, double b) {
    if (a != a) return false;
    if (b != b) return true;
    return a < b;
}

void insertion_less(double* a, std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) {
        double v = a[i];
        std::size_t j = i;
        while (j > 0 && less_d(v, a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

void sort3_less(double* a) {
    if (less_d(a[1], a[0])) std::swap(a[0], a[1]);
    if (less_d(a[2], a[1])) std::swap(a[1], a[2]);
    if (less_d(a[1], a[0])) std::swap(a[0], a[1]);
}

void sort2_less(double* a) {
    if (less_d(a[1], a[0])) std::swap(a[0], a[1]);
}

void double_core(double* a, std::size_t m, int depth, const opts& o,
                 std::vector<double>& buf) {
    if (m < 2) return;
    ++g_stats.nodes;
    g_stats.sum_m += m;
    if (static_cast<std::size_t>(depth) > g_stats.max_depth) {
        g_stats.max_depth = static_cast<std::size_t>(depth);
    }
    if (m < o.small_sort || depth >= o.max_depth) {
        ++g_stats.fallback;
        std::sort(a, a + m, less_d);
        return;
    }

    double mn = a[0];
    double mx = a[0];
    for (std::size_t i = 1; i < m; ++i) {
        const double x = a[i];
        if (x < mn) mn = x;
        if (x > mx) mx = x;
    }
    if (!(mn < mx)) return;  // all equal

    const double range = mx - mn;
    if (!(range > 0.0) || !std::isfinite(range)) {
        std::sort(a, a + m, less_d);
        return;
    }

    const std::size_t auto_cap =
        (m <= 2000000) ? (std::size_t(1) << 16) : (std::size_t(1) << 20);
    const std::size_t cap = (std::min)(o.max_buckets, auto_cap);
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(m, cap));
    const double scale = static_cast<double>(B) / range;

    auto bucket_of = [&](double x) -> std::size_t {
        const double t = (x - mn) * scale;
        const std::size_t b = static_cast<std::size_t>(t);
        return b >= B ? B - 1 : b;
    };

    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++cnt[bucket_of(a[i])];
    }

    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }

    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = bucket_of(a[i]);
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);

    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len <= 1) continue;
        if (len < o.leaf_sort) {
            if (len == 2) {
                sort2_less(a + begin);
            } else if (len == 3) {
                sort3_less(a + begin);
            } else {
                insertion_less(a + begin, len);
            }
        } else if (len < o.small_sort) {
            std::sort(a + begin, a + end, less_d);
        } else {
            double_core(a + begin, len, depth + 1, o, buf);
        }
    }
}

void double_sort(double* a, std::size_t n, const opts& o) {
    if (n < 2) return;
    std::vector<double> buf(n);
    double_core(a, n, 0, o, buf);
}

template <class T, class F>
double median_ms(const std::vector<T>& base, F f, int reps) {
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

    std::mt19937_64 rng(2026);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> base(n);
    for (auto& x : base) x = d(rng);

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    oblivone::options kopt;

    const auto key_core = [&](std::vector<double>& v) {
        oblivone::oblivone_sort(v.data(), v.size(), key, kopt);
    };
    const auto dbl_core = [&](std::vector<double>& v) {
        double_sort(v.data(), v.size(), opts{});
    };

    {
        auto a = base; key_core(a);
        auto b = base; dbl_core(b);
        if (!std::is_sorted(a.begin(), a.end(), less_d) ||
            !std::is_sorted(b.begin(), b.end(), less_d)) {
            std::printf("MISMATCH\n");
            return 2;
        }
    }

    g_stats = {};
    {
        auto b = base;
        double_sort(b.data(), b.size(), opts{});
    }
    std::printf("double stats: nodes=%zu sum_m=%zu max_depth=%zu fallback=%zu\n",
                g_stats.nodes, g_stats.sum_m, g_stats.max_depth, g_stats.fallback);

    const double t_key = median_ms(base, key_core, reps);
    const double t_dbl = median_ms(base, dbl_core, reps);
    std::printf("n=%zu\n", n);
    std::printf("key core    %.3f ms\n", t_key);
    std::printf("double core %.3f ms\n", t_dbl);
    std::printf("speedup: %.3f x (%.1f%% faster)\n", t_key / t_dbl,
                100.0 * (t_key - t_dbl) / t_key);
    return 0;
}

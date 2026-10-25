// Does the double -> uint64 key conversion cost anything?
// Same data: sort doubles with key() vs sort precomputed uint64 keys.
// The uint64 run performs no conversion inside the sort.
//
// Usage: bench_nokey [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

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

bool sorted_d(const std::vector<double>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (oblivone::key_of(v[i]) < oblivone::key_of(v[i - 1])) return false;
    }
    return true;
}

bool sorted_u(const std::vector<std::uint64_t>& v) {
    return std::is_sorted(v.begin(), v.end());
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;

    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> base(n);
    for (auto& x : base) x = d(rng);

    std::vector<std::uint64_t> keys(n);
    for (std::size_t i = 0; i < n; ++i) keys[i] = oblivone::key_of(base[i]);

    const auto std_raw = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
    };
    const auto std_key = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end(), [](double a, double b) {
            return oblivone::key_of(a) < oblivone::key_of(b);
        });
    };
    const auto std_u64 = [](std::vector<std::uint64_t>& v) {
        std::sort(v.begin(), v.end());
    };
    oblivone::options opt;
    const auto ob_double = [&](std::vector<double>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };
    const auto ob_double_key = [&](std::vector<double>& v) {
        oblivone::sort_by(v, [](const double& x) { return oblivone::key_of(x); }, opt);
    };
    const auto ob_u64 = [&](std::vector<std::uint64_t>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };

    {
        auto a = base; std_raw(a);
        auto b = base; std_key(b);
        auto c = keys; std_u64(c);
        auto e = base; ob_double(e);
        auto g = base; ob_double_key(g);
        auto f = keys; ob_u64(f);
        if (!sorted_d(a) || !sorted_d(b) || !sorted_u(c) || !sorted_d(e) ||
            !sorted_d(g) || !sorted_u(f)) {
            std::printf("MISMATCH\n");
            return 2;
        }
    }

    const double t_std_raw = median_ms(base, std_raw, reps);
    const double t_std_key = median_ms(base, std_key, reps);
    const double t_std_u64 = median_ms(keys, std_u64, reps);
    const double t_ob_d = median_ms(base, ob_double, reps);
    const double t_ob_k = median_ms(base, ob_double_key, reps);
    const double t_ob_u = median_ms(keys, ob_u64, reps);

    const double scale = 1e6 / static_cast<double>(n);
    std::printf("n=%zu\n", n);
    std::printf("std_raw   %.3f ms  (%.2f ns/elem)\n", t_std_raw, t_std_raw * scale);
    std::printf("std_key   %.3f ms  (%.2f ns/elem)\n", t_std_key, t_std_key * scale);
    std::printf("std_u64   %.3f ms  (%.2f ns/elem)\n", t_std_u64, t_std_u64 * scale);
    std::printf("ob_key    %.3f ms  (%.2f ns/elem)\n", t_ob_k, t_ob_k * scale);
    std::printf("ob_double %.3f ms  (%.2f ns/elem)\n", t_ob_d, t_ob_d * scale);
    std::printf("ob_u64    %.3f ms  (%.2f ns/elem)\n", t_ob_u, t_ob_u * scale);
    std::printf("native leaf vs key leaf: %.2f ms (%.1f%%)\n", t_ob_d - t_ob_k,
                100.0 * (t_ob_d - t_ob_k) / t_ob_k);
    std::printf("key conversion overhead vs u64: %.2f ms (%.1f%%)\n",
                t_ob_k - t_ob_u, 100.0 * (t_ob_k - t_ob_u) / t_ob_u);
    return 0;
}

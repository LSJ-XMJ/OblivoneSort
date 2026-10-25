// Precompute keys once, then sort (key, value) pairs with no key() conversion
// during the sort. Compare with sorting doubles + key().
//
// Usage: bench_pair [n] [reps]

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

struct Pair {
    std::uint64_t key;
    double value;
};

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

    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> base(n);
    for (auto& x : base) x = d(rng);

    std::vector<Pair> pairs(n);
    for (std::size_t i = 0; i < n; ++i) {
        pairs[i].key = oblivone::key_of(base[i]);
        pairs[i].value = base[i];
    }

    oblivone::options opt;
    const auto sort_double = [&](std::vector<double>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };
    const auto sort_pair = [&](std::vector<Pair>& v) {
        oblivone::sort_by(v, [](const Pair& p) { return p.key; }, opt);
    };

    const double t_d = median_ms(base, sort_double, reps);
    const double t_p = median_ms(pairs, sort_pair, reps);

    const double scale = 1e6 / static_cast<double>(n);
    std::printf("n=%zu\n", n);
    std::printf("double + key()      %.3f ms  (%.2f ns/elem)\n", t_d, t_d * scale);
    std::printf("(key,value) pairs   %.3f ms  (%.2f ns/elem)\n", t_p, t_p * scale);
    std::printf("pair overhead: %.2f ms (%.1f%% of double path)\n", t_p - t_d,
                100.0 * (t_p - t_d) / t_d);
    return 0;
}

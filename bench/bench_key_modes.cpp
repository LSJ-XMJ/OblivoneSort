// Specialized double key modes: can we beat key_of(double) by choosing a
// branch-free mapping based on the data's sign?
//
// Usage: bench_key_modes [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t key_nonneg(double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    return u;  // all values >= 0, finite, no NaN: bit pattern is monotone
}

std::uint64_t key_nonpos(double v) {
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    return ~u;  // all values <= 0: reverse bit pattern is monotone
}

std::uint64_t key_branchless(double v) {
    if (v != v) return std::numeric_limits<std::uint64_t>::max();
    if (v == 0.0) v = 0.0;
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    const std::uint64_t sign = u >> 63;                 // 0 or 1
    const std::uint64_t mask =
        (0ull - sign) | 0x8000000000000000ull;          // signbit or all ones
    return u ^ mask;
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

bool sorted_d(const std::vector<double>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (oblivone::key_of(v[i]) < oblivone::key_of(v[i - 1])) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    oblivone::options opt;

    std::mt19937_64 rng(321);
    std::uniform_real_distribution<double> pos(0.0, 1.0);
    std::uniform_real_distribution<double> neg(-1.0, 0.0);
    std::uniform_real_distribution<double> mix(-1.0, 1.0);

    std::vector<double> a(n), b(n), c(n);
    for (auto& x : a) x = pos(rng);
    for (auto& x : b) x = neg(rng);
    for (auto& x : c) x = mix(rng);

    const auto def = [](const double& x) { return oblivone::key_of(x); };
    const auto nonneg = [](const double& x) { return key_nonneg(x); };
    const auto nonpos = [](const double& x) { return key_nonpos(x); };
    const auto branchless = [](const double& x) { return key_branchless(x); };

    const auto sort_def = [&](std::vector<double>& v) {
        oblivone::sort_by(v, def, opt);
    };
    const auto sort_nonneg = [&](std::vector<double>& v) {
        oblivone::sort_by(v, nonneg, opt);
    };
    const auto sort_nonpos = [&](std::vector<double>& v) {
        oblivone::sort_by(v, nonpos, opt);
    };
    const auto sort_adaptive = [&](std::vector<double>& v) {
        oblivone::sort(v, opt);
    };
    const auto sort_branchless = [&](std::vector<double>& v) {
        oblivone::sort_by(v, branchless, opt);
    };

    {
        auto x = a; sort_nonneg(x);
        auto y = b; sort_nonpos(y);
        auto z = c; sort_adaptive(z);
        auto w = c; sort_branchless(w);
        if (!sorted_d(x) || !sorted_d(y) || !sorted_d(z) || !sorted_d(w)) {
            std::printf("MISMATCH\n");
            return 2;
        }
    }

    const double t_pos_def = median_ms(a, sort_def, reps);
    const double t_pos_nn = median_ms(a, sort_nonneg, reps);
    const double t_pos_ad = median_ms(a, sort_adaptive, reps);
    const double t_neg_def = median_ms(b, sort_def, reps);
    const double t_neg_np = median_ms(b, sort_nonpos, reps);
    const double t_neg_ad = median_ms(b, sort_adaptive, reps);
    const double t_mix_def = median_ms(c, sort_def, reps);
    const double t_mix_ad = median_ms(c, sort_adaptive, reps);
    const double t_mix_bl = median_ms(c, sort_branchless, reps);

    std::printf("n=%zu\n", n);
    std::printf("nonnegative: default %.3f, bitcast %.3f, adaptive %.3f ms\n",
                t_pos_def, t_pos_nn, t_pos_ad);
    std::printf("nonpositive: default %.3f, ~bitcast %.3f, adaptive %.3f ms\n",
                t_neg_def, t_neg_np, t_neg_ad);
    std::printf("mixed:       default %.3f, branchless %.3f, adaptive %.3f ms\n",
                t_mix_def, t_mix_bl, t_mix_ad);
    return 0;
}

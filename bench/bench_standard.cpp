// Standard-pattern benchmark (Bentley–McIlroy style):
//   random, sorted, reverse, organ pipe, sawtooth, runs, all equal,
//   few unique, nearly sorted, push-front, push-back.
// Methodology: 1 warmup + N measured reps; report median.
// Baselines: std::sort, LSD radix, OblivoneSort.
//
// Usage: bench_standard [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

enum Pattern {
    P_RANDOM,
    P_SORTED,
    P_REVERSE,
    P_ORGAN,
    P_SAWTOOTH,
    P_RUNS,
    P_EQUAL,
    P_FEW,
    P_NEAR_01,
    P_NEAR_1,
    P_NEAR_5,
    P_PUSH_FRONT,
    P_PUSH_BACK,
};

const char* pattern_name(int p) {
    switch (p) {
        case P_RANDOM: return "random";
        case P_SORTED: return "sorted";
        case P_REVERSE: return "reverse";
        case P_ORGAN: return "organ";
        case P_SAWTOOTH: return "sawtooth";
        case P_RUNS: return "runs";
        case P_EQUAL: return "equal";
        case P_FEW: return "few";
        case P_NEAR_01: return "near_0.1%";
        case P_NEAR_1: return "near_1%";
        case P_NEAR_5: return "near_5%";
        case P_PUSH_FRONT: return "push_front";
        case P_PUSH_BACK: return "push_back";
    }
    return "?";
}

std::vector<double> make_pattern(int pattern, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);

    switch (pattern) {
        case P_RANDOM:
            break;
        case P_SORTED:
            std::sort(v.begin(), v.end());
            break;
        case P_REVERSE:
            std::sort(v.begin(), v.end());
            std::reverse(v.begin(), v.end());
            break;
        case P_ORGAN:
            std::sort(v.begin(), v.end());
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
            break;
        case P_SAWTOOTH:
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 64);
            break;
        case P_RUNS: {
            const std::size_t run = 64;
            for (std::size_t i = 0; i < n; i += run) {
                const std::size_t end = (std::min)(i + run, n);
                std::sort(v.begin() + static_cast<std::ptrdiff_t>(i),
                          v.begin() + static_cast<std::ptrdiff_t>(end));
            }
            break;
        }
        case P_EQUAL:
            std::fill(v.begin(), v.end(), 1.0);
            break;
        case P_FEW:
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
            break;
        case P_NEAR_01:
        case P_NEAR_1:
        case P_NEAR_5: {
            std::sort(v.begin(), v.end());
            const double frac = (pattern == P_NEAR_01) ? 0.001
                              : (pattern == P_NEAR_1) ? 0.01 : 0.05;
            const std::size_t swaps =
                static_cast<std::size_t>(static_cast<double>(n) * frac);
            std::uniform_int_distribution<std::size_t> idx(0, n - 2);
            for (std::size_t k = 0; k < swaps; ++k) {
                const std::size_t i = idx(rng);
                std::swap(v[i], v[i + 1]);
            }
            break;
        }
        case P_PUSH_FRONT: {
            std::sort(v.begin(), v.end());
            const std::size_t m = n / 100;
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n - m), v.end());
            std::rotate(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n - m),
                        v.end());
            break;
        }
        case P_PUSH_BACK: {
            std::sort(v.begin(), v.end());
            const std::size_t m = n / 100;
            std::rotate(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m), v.end());
            break;
        }
    }
    return v;
}

void radix_sort(std::vector<double>& v) {
    const std::size_t n = v.size();
    if (n < 2) return;

    std::vector<std::uint64_t> keys(n), tmp_keys(n);
    std::vector<double> vals(n), tmp_vals(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys[i] = oblivone::key_of(v[i]);
        vals[i] = v[i];
    }

    for (int pass = 0; pass < 8; ++pass) {
        std::size_t count[256] = {};
        const int shift = pass * 8;
        for (std::size_t i = 0; i < n; ++i) {
            ++count[(keys[i] >> shift) & 0xFFu];
        }
        std::size_t sum = 0;
        for (std::size_t b = 0; b < 256; ++b) {
            const std::size_t c = count[b];
            count[b] = sum;
            sum += c;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t b = (keys[i] >> shift) & 0xFFu;
            const std::size_t pos = count[b]++;
            tmp_keys[pos] = keys[i];
            tmp_vals[pos] = vals[i];
        }
        keys.swap(tmp_keys);
        vals.swap(tmp_vals);
    }
    v.swap(vals);
}

template <class F>
double median_ms(const std::vector<double>& base, F f, int reps) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    return times[static_cast<std::size_t>(reps / 2)];
}

bool sorted_by_key(const std::vector<double>& v) {
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
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 5;

#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1);
#endif

    std::printf("pattern,n,std_ms,radix_ms,oblivone_ms,speedup_std\n");

    for (int p = P_RANDOM; p <= P_PUSH_BACK; ++p) {
        const auto base = make_pattern(p, n, 7000 + static_cast<std::uint64_t>(p));

        // Warmup.
        {
            auto v = base;
            std::sort(v.begin(), v.end());
            v = base;
            radix_sort(v);
            v = base;
            oblivone::sort(v.data(), v.size());
        }

        const auto std_sort = [](std::vector<double>& v) {
            std::sort(v.begin(), v.end());
        };
        const auto radix = [](std::vector<double>& v) { radix_sort(v); };
        oblivone::options opt;
        const auto oblivone = [&](std::vector<double>& v) {
            oblivone::sort(v.data(), v.size(), opt);
        };

        {
            auto a = base; std_sort(a);
            auto b = base; radix(b);
            auto c = base; oblivone(c);
            if (!sorted_by_key(a) || !sorted_by_key(b) || !sorted_by_key(c)) {
                std::printf("%s,%zu,MISMATCH\n", pattern_name(p), n);
                return 2;
            }
        }

        const double t_std = median_ms(base, std_sort, reps);
        const double t_radix = median_ms(base, radix, reps);
        const double t_ob = median_ms(base, oblivone, reps);

        std::printf("%s,%zu,%.3f,%.3f,%.3f,%.2f\n", pattern_name(p), n, t_std,
                    t_radix, t_ob, t_std / t_ob);
    }
    return 0;
}

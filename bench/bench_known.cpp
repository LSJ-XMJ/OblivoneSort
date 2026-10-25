// Known-sort comparison on standard patterns:
//   std::sort, std::stable_sort, pdqsort, Boost.Spreadsort, ska_sort,
//   TimSort, OblivoneSort.
// Optional columns appear as -1 when the header was not available at build time.
//
// Usage: bench_known [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#if defined(__has_include)
#  if __has_include("pdqsort.h")
#    include "pdqsort.h"
#    define HAVE_PDQSORT 1
#  endif
#  if __has_include("ska_sort.hpp") && !defined(_MSC_VER)
#    include "ska_sort.hpp"
#    define HAVE_SKA_SORT 1
#  endif
#  if __has_include(<gfx/timsort.hpp>)
#    include <gfx/timsort.hpp>
#    define HAVE_TIMSORT 1
#  endif
#  if __has_include(<boost/sort/spreadsort/spreadsort.hpp>)
#    include <boost/sort/spreadsort/spreadsort.hpp>
#    define HAVE_SPREADSORT 1
#  endif
#endif

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
        case P_RANDOM: break;
        case P_SORTED: std::sort(v.begin(), v.end()); break;
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
        case P_EQUAL: std::fill(v.begin(), v.end(), 1.0); break;
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
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;

#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1);
#endif

    std::printf("pattern,n,std,stable,pdqsort,spreadsort,ska,timsort,oblivone\n");

    for (int p = P_RANDOM; p <= P_PUSH_BACK; ++p) {
        const auto base = make_pattern(p, n, 9000 + static_cast<std::uint64_t>(p));

        const auto std_sort = [](std::vector<double>& v) {
            std::sort(v.begin(), v.end());
        };
        const auto stable_sort = [](std::vector<double>& v) {
            std::stable_sort(v.begin(), v.end());
        };
#if HAVE_PDQSORT
        const auto pdq = [](std::vector<double>& v) { pdqsort(v.begin(), v.end()); };
#endif
#if HAVE_SPREADSORT
        const auto spread = [](std::vector<double>& v) {
            boost::sort::spreadsort::float_sort(v.begin(), v.end());
        };
#endif
#if HAVE_SKA_SORT
        const auto ska = [](std::vector<double>& v) { ska_sort(v.begin(), v.end()); };
#endif
#if HAVE_TIMSORT
        const auto tim = [](std::vector<double>& v) { gfx::timsort(v.begin(), v.end()); };
#endif
        oblivone::options opt;
        const auto ob = [&](std::vector<double>& v) {
            oblivone::sort(v.data(), v.size(), opt);
        };

        auto check = [&](const char* name, auto f) {
            auto v = base;
            f(v);
            if (!sorted_by_key(v)) {
                std::printf("%s,%zu,%s=MISMATCH\n", pattern_name(p), n, name);
                std::exit(2);
            }
        };
        check("std", std_sort);
        check("stable", stable_sort);
#if HAVE_PDQSORT
        check("pdqsort", pdq);
#endif
#if HAVE_SPREADSORT
        check("spreadsort", spread);
#endif
#if HAVE_SKA_SORT
        check("ska", ska);
#endif
#if HAVE_TIMSORT
        check("timsort", tim);
#endif
        check("oblivone", ob);

        const double t_std = median_ms(base, std_sort, reps);
        const double t_stable = median_ms(base, stable_sort, reps);
#if HAVE_PDQSORT
        const double t_pdq = median_ms(base, pdq, reps);
#else
        const double t_pdq = -1.0;
#endif
#if HAVE_SPREADSORT
        const double t_spread = median_ms(base, spread, reps);
#else
        const double t_spread = -1.0;
#endif
#if HAVE_SKA_SORT
        const double t_ska = median_ms(base, ska, reps);
#else
        const double t_ska = -1.0;
#endif
#if HAVE_TIMSORT
        const double t_tim = median_ms(base, tim, reps);
#else
        const double t_tim = -1.0;
#endif
        const double t_ob = median_ms(base, ob, reps);

        std::printf("%s,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                    pattern_name(p), n, t_std, t_stable, t_pdq, t_spread, t_ska,
                    t_tim, t_ob);
    }
    return 0;
}

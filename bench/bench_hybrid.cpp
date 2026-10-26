// TEST ONLY (uses third-party TimSort):
//   > T blocks: Oblivone core partitions them recursively
//   <= T blocks: left unsorted
//   after all partitioning: gfx::timsort on the whole array
//
// Compare against pure TimSort, sweep T.
//
// Build with -std=c++20 and gfx/timsort.hpp.
// Usage: bench_hybrid [n] [reps]

#include <oblivone/sort.hpp>

#include <gfx/timsort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct noop_fallback {
    template <class T, class Cmp>
    void operator()(T*, T*, Cmp) const {}
};

enum Pattern {
    P_RANDOM,
    P_SORTED,
    P_REVERSE,
    P_ORGAN,
    P_PUSH_FRONT,
    P_PUSH_BACK,
    P_NEAR_5,
    P_FEW,
};

const char* pattern_name(int p) {
    switch (p) {
        case P_RANDOM: return "random";
        case P_SORTED: return "sorted";
        case P_REVERSE: return "reverse";
        case P_ORGAN: return "organ";
        case P_PUSH_FRONT: return "push_front";
        case P_PUSH_BACK: return "push_back";
        case P_NEAR_5: return "near_5%";
        case P_FEW: return "few";
    }
    return "?";
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(7000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);

    switch (p) {
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
        case P_NEAR_5: {
            std::sort(v.begin(), v.end());
            const std::size_t swaps = n / 20;
            std::uniform_int_distribution<std::size_t> idx(0, n - 2);
            for (std::size_t k = 0; k < swaps; ++k) {
                const std::size_t i = idx(rng);
                std::swap(v[i], v[i + 1]);
            }
            break;
        }
        case P_FEW:
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
            break;
    }
    return v;
}

void hybrid(std::vector<double>& v, std::size_t T) {
    oblivone::options opt;
    opt.small_sort = T + 1;       // m <= T -> noop fallback
    opt.leaf_sort = 0;            // no insertion in the core
    opt.max_depth = 64;
    opt.scan_threshold = std::numeric_limits<std::size_t>::max();

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    oblivone::oblivone_sort(v.data(), v.size(), key, opt, noop_fallback{});
    gfx::timsort(v.begin(), v.end());
}

double median_ms(const std::vector<double>& base, std::size_t T, int reps) {
    std::vector<double> times;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        hybrid(v, T);
        const auto t1 = Clock::now();
        if (!std::is_sorted(v.begin(), v.end())) {
            std::printf("MISMATCH T=%zu\n", T);
            std::exit(2);
        }
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

double tim_ms(const std::vector<double>& base, int reps) {
    std::vector<double> times;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        gfx::timsort(v.begin(), v.end());
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

    const std::size_t Ts[] = {0, 8, 16, 32, 64, 128, 256, 512,
                              1024, 2048, 4096, 8192, n};

    std::printf("pattern,n,tim_ms,T,hybrid_ms,ratio\n");
    for (int p = P_RANDOM; p <= P_FEW; ++p) {
        const auto base = make_pattern(p, n);
        const double t_tim = tim_ms(base, reps);
        for (const std::size_t T : Ts) {
            const double t_h = median_ms(base, T, reps);
            std::printf("%s,%zu,%.3f,%zu,%.3f,%.3f\n", pattern_name(p), n, t_tim,
                        T, t_h, t_h / t_tim);
        }
    }
    return 0;
}

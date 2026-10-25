// n=2: insertion vs several compare/swap formulations.
// 1,000,000 groups by default; 5 reps; same key().
//
// Usage: bench_sort2 [groups] [reps]

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

enum Dist { D_UNIFORM, D_NORMAL, D_EXPONENTIAL, D_FEW };

const char* dist_name(int d) {
    switch (d) {
        case D_UNIFORM: return "uniform";
        case D_NORMAL: return "normal";
        case D_EXPONENTIAL: return "exponential";
        case D_FEW: return "few";
    }
    return "?";
}

std::vector<double> make_groups(int dist, std::size_t groups) {
    std::mt19937_64 rng(4321 + static_cast<std::uint64_t>(dist) * 100);
    std::vector<double> v(groups * 2);
    switch (dist) {
        case D_UNIFORM: {
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_NORMAL: {
            std::normal_distribution<double> d(0.0, 1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_EXPONENTIAL: {
            std::exponential_distribution<double> d(1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_FEW: {
            std::uniform_int_distribution<int> d(0, 9);
            for (auto& x : v) x = d(rng);
            break;
        }
    }
    return v;
}

template <class F>
double avg_ns(const std::vector<double>& base, std::size_t groups, F f,
              int reps) {
    const auto key = [](const double& x) { return oblivone::key_of(x); };
    double sum = 0.0;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        for (std::size_t off = 0; off < v.size(); off += 2) {
            if (key(v[off + 1]) < key(v[off])) {
                std::printf("MISMATCH\n");
                std::exit(2);
            }
        }
        sum += std::chrono::duration<double, std::nano>(t1 - t0).count() /
               static_cast<double>(groups);
    }
    return sum / static_cast<double>(reps);
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t groups =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 5;
    const auto key = [](const double& x) { return oblivone::key_of(x); };

    std::printf("dist,insertion,move,swap,cached,branchless,raw_less\n");

    for (int dist = D_UNIFORM; dist <= D_FEW; ++dist) {
        const auto base = make_groups(dist, groups);

        const auto insertion = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                oblivone::detail::insertion_sort(v.data() + off, 2, key);
            }
        };
        const auto move = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                double* a = v.data() + off;
                double x = a[1];
                if (key(x) < key(a[0])) {
                    a[1] = a[0];
                    a[0] = x;
                }
            }
        };
        const auto swap_ver = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                double* a = v.data() + off;
                if (key(a[1]) < key(a[0])) std::swap(a[0], a[1]);
            }
        };
        const auto cached = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                double* a = v.data() + off;
                const auto k0 = key(a[0]);
                const auto k1 = key(a[1]);
                if (k1 < k0) std::swap(a[0], a[1]);
            }
        };
        const auto branchless = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                double* a = v.data() + off;
                const double x = a[0];
                const double y = a[1];
                const bool s = key(y) < key(x);
                a[0] = s ? y : x;
                a[1] = s ? x : y;
            }
        };
        const auto raw_less = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += 2) {
                double* a = v.data() + off;
                if (a[1] < a[0]) std::swap(a[0], a[1]);
            }
        };

        std::printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", dist_name(dist),
                    avg_ns(base, groups, insertion, reps),
                    avg_ns(base, groups, move, reps),
                    avg_ns(base, groups, swap_ver, reps),
                    avg_ns(base, groups, cached, reps),
                    avg_ns(base, groups, branchless, reps),
                    avg_ns(base, groups, raw_less, reps));
    }
    return 0;
}

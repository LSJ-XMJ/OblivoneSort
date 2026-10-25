// n=2/3 leaf sorts: key() comparator vs direct double comparator (NaN last).
// 1,000,000 arrays per distribution, 5 reps.
//
// Usage: bench_leaf_less [groups] [reps]

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

std::vector<double> make_groups(int dist, std::size_t n, std::size_t groups) {
    std::mt19937_64 rng(2468 + static_cast<std::uint64_t>(dist) * 100 + n);
    std::vector<double> v(groups * n);
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
double avg_ns(const std::vector<double>& base, std::size_t n, std::size_t groups,
              F f, int reps) {
    double sum = 0.0;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        for (std::size_t off = 0; off < v.size(); off += n) {
            for (std::size_t i = 1; i < n; ++i) {
                if (oblivone::key_of(v[off + i]) <
                    oblivone::key_of(v[off + i - 1])) {
                    std::printf("MISMATCH\n");
                    std::exit(2);
                }
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
    const auto less = [](const double& a, const double& b) {
        return oblivone::detail::less_double(a, b);
    };

    std::printf("n,dist,insert_key,insert_double,special_key,special_double\n");

    for (int dist = D_UNIFORM; dist <= D_FEW; ++dist) {
        for (std::size_t n = 2; n <= 3; ++n) {
            const auto base = make_groups(dist, n, groups);

            const auto insert_key = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off < v.size(); off += n) {
                    oblivone::detail::insertion_sort(v.data() + off, n, key);
                }
            };
            const auto insert_double = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off < v.size(); off += n) {
                    oblivone::detail::insertion_sort_less(v.data() + off, n, less);
                }
            };
            const auto special_key = [&](std::vector<double>& v) {
                if (n == 2) {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        double* a = v.data() + off;
                        if (key(a[1]) < key(a[0])) std::swap(a[0], a[1]);
                    }
                } else {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        oblivone::detail::sort3(v.data() + off, key);
                    }
                }
            };
            const auto special_double = [&](std::vector<double>& v) {
                if (n == 2) {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        double* a = v.data() + off;
                        if (less(a[1], a[0])) std::swap(a[0], a[1]);
                    }
                } else {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        oblivone::detail::sort3_less(v.data() + off, less);
                    }
                }
            };

            std::printf("%zu,%s,%.2f,%.2f,%.2f,%.2f\n", n, dist_name(dist),
                        avg_ns(base, n, groups, insert_key, reps),
                        avg_ns(base, n, groups, insert_double, reps),
                        avg_ns(base, n, groups, special_key, reps),
                        avg_ns(base, n, groups, special_double, reps));
        }
    }
    return 0;
}

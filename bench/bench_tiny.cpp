// n=2/3: insertion vs the specialized sort2/sort3.
// 1000 groups per distribution, 5 reps, same key().
//
// Usage: bench_tiny [groups] [reps]

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

template <class T, class KeyFn>
void sort2_local(T* a, const KeyFn& key) {
    T v = a[1];
    if (key(v) < key(a[0])) {
        a[1] = a[0];
        a[0] = v;
    }
}

std::vector<double> make_groups(int dist, std::size_t n, std::size_t groups) {
    std::mt19937_64 rng(1234 + static_cast<std::uint64_t>(dist) * 100 + n);
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
    const auto key = [](const double& x) { return oblivone::key_of(x); };
    double sum = 0.0;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        for (std::size_t off = 0; off < v.size(); off += n) {
            for (std::size_t i = 1; i < n; ++i) {
                if (key(v[off + i]) < key(v[off + i - 1])) {
                    std::printf("MISMATCH n=%zu\n", n);
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
                   : 1000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 5;
    const auto key = [](const double& x) { return oblivone::key_of(x); };

    std::printf("n,dist,insertion_ns,special_ns,ratio_ins_over_special\n");

    for (int dist = D_UNIFORM; dist <= D_FEW; ++dist) {
        for (std::size_t n = 2; n <= 3; ++n) {
            const auto base = make_groups(dist, n, groups);

            const auto insertion = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off < v.size(); off += n) {
                    oblivone::detail::insertion_sort(v.data() + off, n, key);
                }
            };
            const auto special = [&](std::vector<double>& v) {
                if (n == 2) {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        sort2_local(v.data() + off, key);
                    }
                } else {
                    for (std::size_t off = 0; off < v.size(); off += n) {
                        oblivone::detail::sort3(v.data() + off, key);
                    }
                }
            };

            const double t_ins = avg_ns(base, n, groups, insertion, reps);
            const double t_sp = avg_ns(base, n, groups, special, reps);
            std::printf("%zu,%s,%.2f,%.2f,%.3f\n", n, dist_name(dist), t_ins, t_sp,
                        t_ins / t_sp);
        }
    }
    return 0;
}

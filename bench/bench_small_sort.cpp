// Real dispatcher small_sort sweep at low n.
// Usage: bench_small_sort

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool less_d(double a, double b) {
    return oblivone::key_of(a) < oblivone::key_of(b);
}

void insertion_less(double* a, std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) {
        double v = a[i];
        const std::uint64_t kv = oblivone::key_of(v);
        std::size_t j = i;
        while (j > 0 && kv < oblivone::key_of(a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

template <class T>
T median(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main() {
    const std::size_t groups = 3000;
    const int reps = 5;
    const std::size_t ns[] = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512};
    const std::size_t ss[] = {0, 16, 32, 48, 64, 96, 128, 192, 256};

    std::printf("n,std,insert,ss0,ss16,ss32,ss48,ss64,ss96,ss128,ss192,ss256\n");
    std::mt19937_64 rng(2027);

    for (const std::size_t n : ns) {
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        std::vector<double> data(groups * n);
        for (auto& x : data) x = d(rng);

        using Run = std::function<void(double*)>;
        std::vector<Run> runs;
        runs.push_back([n](double* p) { std::sort(p, p + n, less_d); });
        runs.push_back([n](double* p) { insertion_less(p, n); });
        for (const std::size_t s : ss) {
            oblivone::options opt;
            opt.small_sort = s;
            runs.push_back([n, opt](double* p) { oblivone::sort(p, n, opt); });
        }

        for (const auto& f : runs) {
            auto w = data;
            for (std::size_t off = 0; off < w.size(); off += n) {
                f(w.data() + off);
            }
            for (std::size_t off = 0; off < w.size(); off += n) {
                if (!std::is_sorted(w.begin() + static_cast<std::ptrdiff_t>(off),
                                    w.begin() + static_cast<std::ptrdiff_t>(off + n),
                                    less_d)) {
                    std::printf("MISMATCH n=%zu\n", n);
                    std::exit(2);
                }
            }
        }

        std::vector<std::vector<double>> samples(runs.size());
        std::vector<std::size_t> order(runs.size());
        std::iota(order.begin(), order.end(), 0);
        for (int r = 0; r < reps; ++r) {
            std::shuffle(order.begin(), order.end(), rng);
            for (const std::size_t idx : order) {
                auto w = data;  // copy outside timer
                const auto t0 = Clock::now();
                for (std::size_t off = 0; off < w.size(); off += n) {
                    runs[idx](w.data() + off);
                }
                const auto t1 = Clock::now();
                samples[idx].push_back(
                    std::chrono::duration<double, std::nano>(t1 - t0).count() /
                    static_cast<double>(groups));
            }
        }

        std::printf("%zu", n);
        for (auto& s : samples) std::printf(",%.2f", median(s));
        std::printf("\n");
    }
    return 0;
}

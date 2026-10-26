// Real-core B sweep: options.bucket_cap, no prototype.
//
// Usage: bench_bcap_real [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool less_d(double a, double b) {
    return oblivone::key_of(a) < oblivone::key_of(b);
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(8100 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (p == 1) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (p == 2) {
        std::sort(v.begin(), v.end());
        std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    }
    return v;
}

template <class T>
T median(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t Bs[] = {16384, 32768, 65536, 131072, 262144, 524288,
                              1048576};

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    const std::size_t nB = sizeof(Bs) / sizeof(Bs[0]);
    std::printf("pattern,n,B,ms\n");

    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);
        std::vector<oblivone::options> opts(nB);
        for (std::size_t i = 0; i < nB; ++i) {
            opts[i].max_buckets = (std::max)(std::size_t(1) << 20, Bs[i]);
            opts[i].bucket_cap = Bs[i];
        }

        for (std::size_t i = 0; i < nB; ++i) {
            auto check = data;
            oblivone::oblivone_sort(check.data(), check.size(), key, opts[i]);
            if (!std::is_sorted(check.begin(), check.end(), less_d)) {
                std::printf("MISMATCH\n");
                return 2;
            }
        }

        std::vector<std::vector<double>> samples(nB);
        std::vector<std::size_t> order(nB);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 rng(4242 + static_cast<std::uint64_t>(p));

        for (int r = 0; r < (std::max)(1, reps); ++r) {
            std::shuffle(order.begin(), order.end(), rng);
            for (const std::size_t i : order) {
                auto v = data;
                const auto t0 = Clock::now();
                oblivone::oblivone_sort(v.data(), v.size(), key, opts[i]);
                const auto t1 = Clock::now();
                samples[i].push_back(
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }

        for (std::size_t i = 0; i < nB; ++i) {
            std::printf("%s,%zu,%zu,%.3f\n",
                        p == 0 ? "random" : (p == 1 ? "few" : "organ"), n,
                        Bs[i], median(samples[i]));
        }
    }
    return 0;
}

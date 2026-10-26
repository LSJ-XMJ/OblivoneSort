// Benchmark the parallel API.
// Usage: bench_parallel_api [n] [reps] [threads]

#include <oblivone/parallel.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool less_d(double a, double b) {
    return oblivone::key_of(a) < oblivone::key_of(b);
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(15000 + static_cast<std::uint64_t>(p));
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

template <class F>
double median_ms(const std::vector<double>& base, F f, int reps) {
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
                   : 10000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const unsigned threads = (argc > 3) ? static_cast<unsigned>(std::atoi(argv[3])) : 0;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    std::printf("pattern,n,threads,serial_ms,par_ms,ratio\n");
    const char* names[] = {"random", "few", "organ"};

    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);

        auto serial = data;
        oblivone::oblivone_sort(serial.data(), serial.size(), key,
                                oblivone::options{});
        auto par = data;
        oblivone::parallel_options popt;
        popt.threads = threads;
        popt.min_n = 1000000;
        oblivone::parallel_sort(par, oblivone::options{}, popt);
        if (!std::is_sorted(serial.begin(), serial.end(), less_d) ||
            !std::is_sorted(par.begin(), par.end(), less_d)) {
            std::printf("MISMATCH\n");
            return 2;
        }

        const double ts = median_ms(data, [&](std::vector<double>& v) {
            oblivone::oblivone_sort(v.data(), v.size(), key, oblivone::options{});
        }, reps);
        const double tp = median_ms(data, [&](std::vector<double>& v) {
            oblivone::parallel_sort(v, oblivone::options{}, popt);
        }, reps);
        std::printf("%s,%zu,%u,%.3f,%.3f,%.3f\n", names[p], n, threads, ts, tp,
                    tp / ts);
    }
    return 0;
}

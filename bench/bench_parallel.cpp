#include <oblivone/parallel.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool less_double(double a, double b) {
    return oblivone::detail::less_double(a, b);
}

template <class F>
double median_ms(const std::vector<double>& base, F fn, int reps) {
    std::vector<double> times;
    for (int i = 0; i < reps; ++i) {
        auto v = base;
        const auto t0 = Clock::now();
        fn(v);
        const auto t1 = Clock::now();
        times.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
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
    const unsigned threads =
        (argc > 3) ? static_cast<unsigned>(std::atoi(argv[3])) : 0;
    const std::size_t min_n =
        (argc > 4) ? static_cast<std::size_t>(std::strtoull(argv[4], nullptr, 10))
                   : 2000000;

    std::mt19937_64 rng(99);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<double> base(n);
    for (auto& x : base) x = dist(rng);

    const double serial = median_ms(base, [](std::vector<double>& v) {
        oblivone::sort(v);
    }, reps);

    oblivone::parallel_options popt;
    popt.threads = threads;
    popt.min_n = min_n;

    const double parallel = median_ms(base, [&](std::vector<double>& v) {
        oblivone::parallel_sort(v, oblivone::options{}, popt);
    }, reps);

    auto verify = base;
    oblivone::parallel_sort(verify, oblivone::options{}, popt);
    if (!std::is_sorted(verify.begin(), verify.end(), less_double)) {
        std::printf("FAIL\n");
        return 2;
    }

    std::printf("n,threads,min_n,serial_ms,parallel_ms,ratio\n");
    std::printf("%zu,%u,%zu,%.3f,%.3f,%.3f\n", n, threads, min_n, serial,
                parallel, parallel / serial);
    return 0;
}

#include <oblivone/advanced/sort.hpp>
#include <oblivone/parallel.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
bool less_double(double a, double b) {
    return oblivone::detail::less_double(a, b);
}
std::vector<double> make(int p, std::size_t n) {
    std::mt19937_64 rng(7070 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (p == 1) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (p == 2) {
        for (std::size_t i = 0; i < n; ++i) {
            v[i] = static_cast<double>(i < n / 2 ? i : n - i);
        }
    }
    return v;
}
template <class F>
double median(const std::vector<double>& base, F fn, int reps) {
    std::vector<double> times;
    for (int i = 0; i < reps; ++i) {
        auto v = base;
        const auto a = Clock::now();
        fn(v);
        const auto b = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        if (!std::is_sorted(v.begin(), v.end(), less_double)) return 1e9;
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}
}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 2;
    const unsigned threads =
        (argc > 3) ? static_cast<unsigned>(std::atoi(argv[3])) : 4;
    const bool sample = (argc > 4) ? (std::atoi(argv[4]) != 0) : false;
    const char* names[] = {"random", "few", "organ"};
    std::printf("pattern,n,serial,simple,advanced,adv/simple,sample\n");
    for (int p = 0; p < 3; ++p) {
        auto base = make(p, n);
        double s = median(base, [](std::vector<double>& v) {
            oblivone::sort(v);
        }, reps);
        double a = median(base, [threads](std::vector<double>& v) {
            oblivone::parallel_options p;
            p.threads = threads;
            p.min_n = 1000;
            oblivone::parallel_sort(v, oblivone::options{}, p);
        }, reps);
        double b = median(base, [threads, sample](std::vector<double>& v) {
            oblivone::advanced::options p;
            p.threads = threads;
            p.min_n = 1000;
            p.sample_buckets = sample;
            oblivone::advanced::parallel_sort(v, oblivone::options{}, p);
        }, reps);
        std::printf("%s,%zu,%.3f,%.3f,%.3f,%.3f,%d\n", names[p], n, s, a, b,
                    b / a, sample ? 1 : 0);
    }
}

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool less_double(double a, double b) {
    return oblivone::detail::less_double(a, b);
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(1000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    std::vector<double> v(n);
    for (auto& x : v) x = dist(rng);

    switch (p) {
        case 0:
            break;
        case 1:
            std::sort(v.begin(), v.end());
            break;
        case 2:
            std::sort(v.begin(), v.end(), std::greater<double>());
            break;
        case 3:
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<double>(i < n / 2 ? i : n - i);
            }
            break;
        case 4:
            for (auto& x : v) x = static_cast<double>(rng() % 10);
            break;
        case 5:
            std::sort(v.begin(), v.end());
            for (std::size_t i = 0; i < n / 1000; ++i) {
                const std::size_t a = rng() % n;
                const std::size_t b = rng() % n;
                std::swap(v[a], v[b]);
            }
            break;
        default:
            for (auto& x : v) x = 0.0;
            break;
    }
    return v;
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
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;

    const char* names[] = {"random", "sorted", "reverse", "organ",
                           "few", "near0.1%", "equal"};

    std::printf("pattern,n,std_ms,oblivone_ms,ratio\n");
    for (int p = 0; p < 7; ++p) {
        const auto base = make_pattern(p, n);

        const double std_ms = median_ms(base, [](std::vector<double>& v) {
            std::sort(v.begin(), v.end(), less_double);
        }, reps);

        const double our_ms = median_ms(base, [](std::vector<double>& v) {
            oblivone::sort(v);
        }, reps);

        auto verify = base;
        oblivone::sort(verify);
        if (!std::is_sorted(verify.begin(), verify.end(), less_double)) {
            std::printf("FAIL %s\n", names[p]);
            return 2;
        }

        std::printf("%s,%zu,%.3f,%.3f,%.3f\n", names[p], n, std_ms, our_ms,
                    our_ms / std_ms);
    }
    return 0;
}

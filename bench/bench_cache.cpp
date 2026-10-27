#include <oblivone/sort.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t expensive_key(double x) {
    volatile double acc = x;
    for (int i = 0; i < 32; ++i) acc = acc * 1.000001 + 0.000001;
    (void)acc;
    return oblivone::key_of(x);
}

bool less_double(double a, double b) {
    return oblivone::detail::less_double(a, b);
}

std::vector<double> make_values(int pattern, std::size_t n) {
    std::mt19937_64 rng(5150 + static_cast<std::uint64_t>(pattern));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (pattern == 1) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (pattern == 2) {
        std::sort(v.begin(), v.end());
        for (std::size_t i = 0; i < n / 100; ++i) {
            std::swap(v[rng() % n], v[rng() % n]);
        }
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

    oblivone::options base;
    oblivone::options cached = base;
    cached.cache_bucket_index = true;

    const char* patterns[] = {"random", "few", "near_1%"};
    std::printf("key,pattern,n,mode,ms,ratio\n");

    for (int key_mode = 0; key_mode < 2; ++key_mode) {
        const char* key_name = key_mode == 0 ? "default" : "expensive";
        for (int p = 0; p < 3; ++p) {
            const auto input = make_values(p, n);

            auto run = [&](int mode, std::vector<double>& v) {
                if (key_mode == 0) {
                    oblivone::sort(v, mode == 0 ? base : cached);
                } else {
                    oblivone::sort_by(v, expensive_key,
                                      mode == 0 ? base : cached);
                }
            };

            for (int mode = 0; mode < 2; ++mode) {
                auto check = input;
                run(mode, check);
                if (!std::is_sorted(check.begin(), check.end(), less_double)) {
                    std::printf("FAIL %s %s\n", key_name, patterns[p]);
                    return 2;
                }
            }

            std::vector<double> plain;
            std::vector<double> cache;
            std::mt19937_64 order_rng(6160 + static_cast<std::uint64_t>(p));
            for (int r = 0; r < reps; ++r) {
                std::array<int, 2> order = {0, 1};
                std::shuffle(order.begin(), order.end(), order_rng);
                for (int mode : order) {
                    auto v = input;
                    const auto t0 = Clock::now();
                    run(mode, v);
                    const auto t1 = Clock::now();
                    const double ms =
                        std::chrono::duration<double, std::milli>(t1 - t0).count();
                    if (mode == 0) {
                        plain.push_back(ms);
                    } else {
                        cache.push_back(ms);
                    }
                }
            }

            const double a = median(plain);
            const double b = median(cache);
            std::printf("%s,%s,%zu,plain,%.3f,1.000\n", key_name, patterns[p],
                        n, a);
            std::printf("%s,%s,%zu,cached,%.3f,%.3f\n", key_name, patterns[p],
                        n, b, b / a);
        }
    }
    return 0;
}

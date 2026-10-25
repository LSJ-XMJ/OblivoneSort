// Randomized differential fuzzer:
//   random type x distribution x size x options -> compare with std::sort.
// Deterministic seed; exit non-zero on the first mismatch.
//
// Usage: fuzz_sort [iterations] [seed]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {

std::mt19937_64 g_rng;

enum Dist {
    D_UNIFORM,
    D_NORMAL,
    D_EXPONENTIAL,
    D_FEW,
    D_SORTED,
    D_REVERSE,
    D_ORGAN,
    D_SAWTOOTH,
    D_RUNS,
    D_EQUAL,
    D_NEAR_DUP,
    D_OUTLIER,
};

const char* dist_name(int d) {
    switch (d) {
        case D_UNIFORM: return "uniform";
        case D_NORMAL: return "normal";
        case D_EXPONENTIAL: return "exponential";
        case D_FEW: return "few";
        case D_SORTED: return "sorted";
        case D_REVERSE: return "reverse";
        case D_ORGAN: return "organ";
        case D_SAWTOOTH: return "sawtooth";
        case D_RUNS: return "runs";
        case D_EQUAL: return "equal";
        case D_NEAR_DUP: return "near_dup";
        case D_OUTLIER: return "outlier";
    }
    return "?";
}

template <class T>
const char* type_name() {
    if constexpr (std::is_same_v<T, std::int32_t>) return "int32";
    if constexpr (std::is_same_v<T, std::int64_t>) return "int64";
    if constexpr (std::is_same_v<T, std::uint32_t>) return "uint32";
    if constexpr (std::is_same_v<T, std::uint64_t>) return "uint64";
    if constexpr (std::is_same_v<T, float>) return "float";
    if constexpr (std::is_same_v<T, double>) return "double";
    return "?";
}

std::vector<double> raw_values(int dist, std::size_t n) {
    std::vector<double> v(n);
    switch (dist) {
        case D_UNIFORM: {
            std::uniform_real_distribution<double> d(-1e6, 1e6);
            for (auto& x : v) x = d(g_rng);
            break;
        }
        case D_NORMAL: {
            std::normal_distribution<double> d(0.0, 1e5);
            for (auto& x : v) x = d(g_rng);
            break;
        }
        case D_EXPONENTIAL: {
            std::exponential_distribution<double> d(1.0);
            for (auto& x : v) x = d(g_rng) * 1e4;
            break;
        }
        case D_FEW: {
            std::uniform_int_distribution<int> d(0, 9);
            for (auto& x : v) x = d(g_rng);
            break;
        }
        case D_SORTED:
        case D_REVERSE: {
            std::uniform_real_distribution<double> d(-1e6, 1e6);
            for (auto& x : v) x = d(g_rng);
            std::sort(v.begin(), v.end());
            if (dist == D_REVERSE) std::reverse(v.begin(), v.end());
            break;
        }
        case D_ORGAN: {
            std::uniform_real_distribution<double> d(-1e6, 1e6);
            for (auto& x : v) x = d(g_rng);
            std::sort(v.begin(), v.end());
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
            break;
        }
        case D_SAWTOOTH: {
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 64);
            break;
        }
        case D_RUNS: {
            const std::size_t run = 64;
            std::uniform_real_distribution<double> d(-1e6, 1e6);
            for (std::size_t i = 0; i < n; i += run) {
                const std::size_t end = (std::min)(i + run, n);
                for (std::size_t j = i; j < end; ++j) v[j] = d(g_rng);
                std::sort(v.begin() + static_cast<std::ptrdiff_t>(i),
                          v.begin() + static_cast<std::ptrdiff_t>(end));
            }
            break;
        }
        case D_EQUAL: {
            for (auto& x : v) x = 1.0;
            break;
        }
        case D_NEAR_DUP: {
            std::uniform_real_distribution<double> d(-1e-12, 1e-12);
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = 1.0 + static_cast<double>(i) * 1e-9 + d(g_rng);
            }
            std::shuffle(v.begin(), v.end(), g_rng);
            break;
        }
        case D_OUTLIER: {
            std::uniform_real_distribution<double> d(-1e6, 1e6);
            for (auto& x : v) x = d(g_rng);
            if (n > 0) v[0] = 1e15;
            break;
        }
    }
    return v;
}

template <class T>
T cast_value(double x) {
    if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_unsigned_v<T>) {
            x = (std::max)(0.0, x);
        }
        x = (std::max)(-1e9, (std::min)(x, 1e9));
        return static_cast<T>(std::llround(x));
    } else {
        return static_cast<T>(x);
    }
}

template <class T>
std::vector<T> make_data(int dist, std::size_t n) {
    auto raw = raw_values(dist, n);
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = cast_value<T>(raw[i]);

    if constexpr (std::is_floating_point_v<T>) {
        // Inject special values in a fraction of the cases.
        if ((g_rng() % 50) == 0 && n > 0) {
            switch (g_rng() % 4) {
                case 0: v[g_rng() % n] = std::numeric_limits<T>::quiet_NaN(); break;
                case 1: v[g_rng() % n] = std::numeric_limits<T>::infinity(); break;
                case 2: v[g_rng() % n] = -std::numeric_limits<T>::infinity(); break;
                default: v[g_rng() % n] = static_cast<T>(-0.0); break;
            }
        }
    }
    return v;
}

template <class T>
std::size_t pick(std::initializer_list<std::size_t> xs) {
    auto it = xs.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(g_rng() % xs.size()));
    return *it;
}

template <class T>
void run_one(int dist, std::size_t n, std::size_t small_sort,
             std::size_t leaf_sort, int max_depth, std::size_t max_buckets) {
    auto v = make_data<T>(dist, n);
    auto expected = v;

    const auto key = [](const T& x) { return oblivone::key_of(x); };
    std::sort(expected.begin(), expected.end(),
              [&](const T& a, const T& b) { return key(a) < key(b); });

    oblivone::options opt;
    opt.small_sort = small_sort;
    opt.leaf_sort = leaf_sort;
    opt.max_depth = max_depth;
    opt.max_buckets = max_buckets;

    oblivone::sort(v.data(), v.size(), opt);

    bool ok = true;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (key(v[i]) != key(expected[i])) {
            ok = false;
            bad = i;
            break;
        }
    }

    if (!ok) {
        std::printf(
            "FAIL type=%s dist=%s n=%zu small=%zu leaf=%zu depth=%d buckets=%zu "
            "at=%zu\n",
            type_name<T>(), dist_name(dist), n, small_sort, leaf_sort, max_depth,
            max_buckets, bad);
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t iterations =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 200000;
    const std::uint64_t seed =
        (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0x5EED1234ull;

    g_rng.seed(seed);

    for (std::size_t it = 0; it < iterations; ++it) {
        const int dist = static_cast<int>(g_rng() % 12);
        std::size_t n;
        const unsigned r = static_cast<unsigned>(g_rng() % 100);
        if (r < 70) {
            n = static_cast<std::size_t>(g_rng() % 65);
        } else if (r < 90) {
            n = 65 + static_cast<std::size_t>(g_rng() % 960);
        } else {
            n = 1024 + static_cast<std::size_t>(g_rng() % 20000);
        }

        const std::size_t small_sort = pick<int>({0, 1, 4, 16, 32});
        const std::size_t leaf_sort = pick<int>({0, 1, 8, 16, 22, 32, 64});
        const int max_depth = static_cast<int>(pick<int>({1, 2, 4, 16}));
        const std::size_t max_buckets =
            pick<int>({0, 1, 4, 64, std::size_t(1) << 16});

        switch (g_rng() % 6) {
            case 0:
                run_one<std::int32_t>(dist, n, small_sort, leaf_sort, max_depth,
                                      max_buckets);
                break;
            case 1:
                run_one<std::int64_t>(dist, n, small_sort, leaf_sort, max_depth,
                                      max_buckets);
                break;
            case 2:
                run_one<std::uint32_t>(dist, n, small_sort, leaf_sort, max_depth,
                                       max_buckets);
                break;
            case 3:
                run_one<std::uint64_t>(dist, n, small_sort, leaf_sort, max_depth,
                                       max_buckets);
                break;
            case 4:
                run_one<float>(dist, n, small_sort, leaf_sort, max_depth,
                               max_buckets);
                break;
            default:
                run_one<double>(dist, n, small_sort, leaf_sort, max_depth,
                                max_buckets);
                break;
        }

        if ((it % 50000) == 0 && it != 0) {
            std::printf("  %zu cases\n", it);
        }
    }

    std::printf("fuzz: %zu cases passed (seed=%llu)\n", iterations,
                static_cast<unsigned long long>(seed));
    return 0;
}

#include <oblivone/sort.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {

template <class T>
std::uint64_t key(const T& value) {
    return oblivone::detail::key_of(value);
}

template <class T>
void verify(const std::vector<T>& values, const oblivone::options& opt) {
    std::vector<T> reference = values;
    std::sort(reference.begin(), reference.end(),
              [](const T& a, const T& b) { return key(a) < key(b); });

    std::vector<T> actual = values;
    oblivone::sort(actual, opt);

    for (std::size_t i = 1; i < actual.size(); ++i) {
        if (key(actual[i]) < key(actual[i - 1])) {
            std::printf("FAIL order n=%zu\n", actual.size());
            std::exit(2);
        }
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (key(actual[i]) != key(reference[i])) {
            std::printf("FAIL multiset n=%zu at %zu\n", actual.size(), i);
            std::exit(2);
        }
    }
}

template <class T>
std::vector<T> make_values(std::mt19937_64& rng, std::size_t n, int pattern) {
    std::vector<T> v(n);
    if constexpr (std::is_floating_point<T>::value) {
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);
        std::normal_distribution<double> normal(0.0, 1.0);
        std::exponential_distribution<double> exponential(1.0);

        switch (pattern % 7) {
            case 0:
                for (auto& x : v) x = static_cast<T>(uniform(rng));
                break;
            case 1:
                for (auto& x : v) x = static_cast<T>(normal(rng));
                break;
            case 2:
                for (auto& x : v) x = static_cast<T>(exponential(rng));
                break;
            case 3:
                for (auto& x : v) x = static_cast<T>(rng() % 10);
                break;
            case 4:
                for (auto& x : v) x = T(1);
                break;
            default:
                for (auto& x : v) x = static_cast<T>(uniform(rng));
                break;
        }
    } else {
        std::uniform_int_distribution<std::int64_t> dist(
            (std::numeric_limits<T>::min)(),
            (std::numeric_limits<T>::max)());
        if (pattern % 3 == 0) {
            for (auto& x : v) x = static_cast<T>(rng() % 10);
        } else {
            for (auto& x : v) x = static_cast<T>(dist(rng));
        }
    }

    if (pattern % 7 == 5) {
        std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
            return key(a) < key(b);
        });
    } else if (pattern % 7 == 6) {
        std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
            return key(a) < key(b);
        });
        std::reverse(v.begin(), v.end());
    }
    return v;
}

oblivone::options random_options(std::mt19937_64& rng) {
    std::uniform_int_distribution<int> d(0, 1000);
    oblivone::options opt;
    opt.small_top = static_cast<std::size_t>(d(rng) % 129);
    opt.small_sort = static_cast<std::size_t>(d(rng) % 25);
    opt.leaf_sort = static_cast<std::size_t>(d(rng) % 33);
    opt.scan_threshold = static_cast<std::size_t>(d(rng) % 513);
    opt.max_depth = 1 + d(rng) % 12;
    opt.bucket_limit = static_cast<std::size_t>(d(rng) % 4097);
    return opt;
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t iterations =
        (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 100000;
    const std::uint64_t seed =
        (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> choose(0, 1000);

    for (std::uint64_t it = 0; it < iterations; ++it) {
        const std::size_t n = static_cast<std::size_t>(choose(rng) % 2000);
        const int pattern = choose(rng) % 7;
        const oblivone::options opt = random_options(rng);

        switch (choose(rng) % 4) {
            case 0:
                verify(make_values<double>(rng, n, pattern), opt);
                break;
            case 1:
                verify(make_values<float>(rng, n, pattern), opt);
                break;
            case 2:
                verify(make_values<std::int32_t>(rng, n, pattern), opt);
                break;
            default:
                verify(make_values<std::uint32_t>(rng, n, pattern), opt);
                break;
        }

        if (it % 50000 == 0 && it != 0) {
            std::printf("%llu cases\n",
                        static_cast<unsigned long long>(it));
        }
    }

    std::printf("fuzz passed: %llu cases, seed=%llu\n",
                static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed));
    return 0;
}

#include <oblivone/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <type_traits>
#include <vector>

namespace {

std::uint64_t key_of(double x) { return oblivone::key_of(x); }
std::uint64_t key_of(float x) { return oblivone::key_of(x); }
std::uint64_t key_of(std::int32_t x) { return oblivone::key_of(x); }
std::uint64_t key_of(std::uint32_t x) { return oblivone::key_of(x); }
std::uint64_t key_of(std::int64_t x) { return oblivone::key_of(x); }
std::uint64_t key_of(std::uint64_t x) { return oblivone::key_of(x); }

template <class T>
std::vector<T> make_values(std::mt19937_64& rng, std::size_t n, int pattern) {
    std::vector<T> v(n);
    if constexpr (std::is_floating_point<T>::value) {
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        for (auto& x : v) x = static_cast<T>(d(rng));
        if (pattern == 5 && !v.empty()) {
            v[rng() % n] = std::numeric_limits<T>::quiet_NaN();
        }
        if (pattern == 6 && !v.empty()) {
            v[rng() % n] = std::numeric_limits<T>::infinity();
        }
    } else {
        std::uniform_int_distribution<T> d(
            (std::numeric_limits<T>::min)(),
            (std::numeric_limits<T>::max)());
        for (auto& x : v) x = d(rng);
    }

    if (pattern == 1) {
        std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
            return key_of(a) < key_of(b);
        });
    } else if (pattern == 2) {
        std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
            return key_of(a) < key_of(b);
        });
        std::reverse(v.begin(), v.end());
    } else if (pattern == 3) {
        for (auto& x : v) x = T(1);
    }
    return v;
}

oblivone::options random_options(std::mt19937_64& rng) {
    std::uniform_int_distribution<int> d(0, 1000);
    oblivone::options opt;
    opt.small_top = static_cast<std::size_t>(d(rng) % 65);
    opt.small_sort = static_cast<std::size_t>(d(rng) % 25);
    opt.leaf_sort = static_cast<std::size_t>(d(rng) % 33);
    opt.scan_threshold = static_cast<std::size_t>(d(rng) % 513);
    opt.max_depth = 1 + d(rng) % 12;

    switch (d(rng) % 7) {
        case 0: opt.bucket_limit = 0; break;
        case 1: opt.bucket_limit = 1; break;
        case 2: opt.bucket_limit = 2; break;
        case 3: opt.bucket_limit = 65535; break;
        case 4: opt.bucket_limit = 65536; break;
        case 5: opt.bucket_limit = 65537; break;
        default:
            opt.bucket_limit = (std::numeric_limits<std::size_t>::max)();
            break;
    }
    return opt;
}

template <class T>
void verify(std::vector<T> input, const oblivone::options& opt) {
    auto plain = input;
    auto cached = input;
    oblivone::options plain_opt = opt;
    plain_opt.cache_bucket_index = false;
    oblivone::options cached_opt = opt;
    cached_opt.cache_bucket_index = true;

    oblivone::sort(plain, plain_opt);
    oblivone::sort(cached, cached_opt);

    for (std::size_t i = 1; i < plain.size(); ++i) {
        if (key_of(plain[i]) < key_of(plain[i - 1])) {
            std::printf("FAIL plain order\n");
            std::exit(2);
        }
        if (key_of(cached[i]) < key_of(cached[i - 1])) {
            std::printf("FAIL cached order\n");
            std::exit(2);
        }
    }
    for (std::size_t i = 0; i < plain.size(); ++i) {
        if (key_of(plain[i]) != key_of(cached[i])) {
            std::printf("FAIL key mismatch at %zu\n", i);
            std::exit(2);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t iterations =
        (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 20000;
    const std::uint64_t seed =
        (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> choose(0, 1000);

    for (std::uint64_t it = 0; it < iterations; ++it) {
        std::size_t n = static_cast<std::size_t>(choose(rng) % 2000);
        if (it % 500 == 0) {
            n = 65536 + static_cast<std::size_t>(choose(rng) % 2000);
        }

        const int pattern = choose(rng) % 7;
        const oblivone::options opt = random_options(rng);

        switch (choose(rng) % 6) {
            case 0: verify(make_values<double>(rng, n, pattern), opt); break;
            case 1: verify(make_values<float>(rng, n, pattern), opt); break;
            case 2: verify(make_values<std::int32_t>(rng, n, pattern), opt); break;
            case 3: verify(make_values<std::uint32_t>(rng, n, pattern), opt); break;
            case 4: verify(make_values<std::int64_t>(rng, n, pattern), opt); break;
            default: verify(make_values<std::uint64_t>(rng, n, pattern), opt); break;
        }
    }

    std::printf("fuzz_cache passed: %llu cases, seed=%llu\n",
                static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed));
    return 0;
}

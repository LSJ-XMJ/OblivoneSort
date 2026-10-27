#include <oblivone/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

template <class T>
bool sorted_by_key(const std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (oblivone::key_of(v[i]) < oblivone::key_of(v[i - 1])) return false;
    }
    return true;
}

template <class T>
void compare(const char* name, const std::vector<T>& input,
             const oblivone::options& base) {
    auto plain = input;
    auto cached = input;

    oblivone::options plain_opt = base;
    plain_opt.cache_bucket_index = false;
    oblivone::options cached_opt = base;
    cached_opt.cache_bucket_index = true;

    oblivone::sort(plain, plain_opt);
    oblivone::sort(cached, cached_opt);

    if (!sorted_by_key(plain) || !sorted_by_key(cached)) {
        std::printf("FAIL sorted: %s\n", name);
        ++failures;
        return;
    }
    for (std::size_t i = 0; i < plain.size(); ++i) {
        if (oblivone::key_of(plain[i]) != oblivone::key_of(cached[i])) {
            std::printf("FAIL key mismatch: %s at %zu\n", name, i);
            ++failures;
            return;
        }
    }
}

template <class T>
std::vector<T> random_values(std::mt19937_64& rng, std::size_t n) {
    std::vector<T> v(n);
    if constexpr (std::is_floating_point<T>::value) {
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        for (auto& x : v) x = static_cast<T>(d(rng));
    } else {
        std::uniform_int_distribution<T> d(
            (std::numeric_limits<T>::min)(),
            (std::numeric_limits<T>::max)());
        for (auto& x : v) x = d(rng);
    }
    return v;
}

template <class T>
void type_matrix(std::mt19937_64& rng) {
    oblivone::options opt;
    opt.bucket_limit = 65536;
    compare<T>("random", random_values<T>(rng, 200000), opt);

    std::vector<T> equal(5000, T(1));
    compare<T>("equal", equal, opt);
}

}  // namespace

int main() {
    std::mt19937_64 rng(90210);

    type_matrix<double>(rng);
    type_matrix<float>(rng);
    type_matrix<std::int32_t>(rng);
    type_matrix<std::uint32_t>(rng);
    type_matrix<std::int64_t>(rng);
    type_matrix<std::uint64_t>(rng);

    {
        oblivone::options opt;
        opt.bucket_limit = 1;
        std::vector<double> v = {
            3.0, std::numeric_limits<double>::quiet_NaN(), -0.0, 0.0,
            -1.0, std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()};
        compare("edge", v, opt);
    }

    {
        const std::size_t sizes[] = {65535, 65536, 65537, 100000};
        const std::size_t limits[] = {
            65535, 65536, 65537,
            (std::numeric_limits<std::size_t>::max)()};
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        for (std::size_t n : sizes) {
            for (std::size_t limit : limits) {
                oblivone::options opt;
                opt.bucket_limit = limit;
                std::vector<double> v(n);
                for (auto& x : v) x = d(rng);
                compare("bucket boundary", v, opt);
            }
        }
    }

    {
        oblivone::options opt;
        opt.bucket_limit = 65536;
        std::vector<double> v(100000);
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        for (auto& x : v) x = d(rng);

        auto plain = v;
        auto cached = v;
        auto key = [](double x) { return oblivone::key_of(x); };
        oblivone::sort_by(plain, key, opt);
        oblivone::options cached_opt = opt;
        cached_opt.cache_bucket_index = true;
        oblivone::sort_by(cached, key, cached_opt);
        if (!sorted_by_key(cached)) {
            std::printf("FAIL custom key sorted\n");
            ++failures;
        }
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (oblivone::key_of(plain[i]) != oblivone::key_of(cached[i])) {
                std::printf("FAIL custom key mismatch at %zu\n", i);
                ++failures;
                break;
            }
        }
    }

    {
        oblivone::options opt;
        std::vector<double> none;
        compare("empty", none, opt);
        compare("one", std::vector<double>{1.0}, opt);
        compare("two", std::vector<double>{2.0, 1.0}, opt);
    }

    if (failures == 0) {
        std::printf("test_cache passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}

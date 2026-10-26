#include <oblivone/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace {

int g_failures = 0;

template <class T>
bool key_sorted(const std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (oblivone::detail::key_of(v[i]) <
            oblivone::detail::key_of(v[i - 1])) {
            return false;
        }
    }
    return true;
}

template <class T>
void check(const char* name, std::vector<T> v, const oblivone::options& opt = {}) {
    std::vector<T> original = v;
    oblivone::sort(v, opt);
    if (!key_sorted(v)) {
        std::printf("FAIL sorted: %s\n", name);
        ++g_failures;
    }
    std::sort(original.begin(), original.end(), [](const T& a, const T& b) {
        return oblivone::detail::key_of(a) < oblivone::detail::key_of(b);
    });
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (oblivone::detail::key_of(v[i]) !=
            oblivone::detail::key_of(original[i])) {
            std::printf("FAIL multiset: %s at %zu\n", name, i);
            ++g_failures;
            break;
        }
    }
}

struct Item {
    std::int32_t key;
    double value;
};

struct CountingFallback {
    int* calls = nullptr;

    template <class T, class Less>
    void operator()(T* first, T* last, Less less) const {
        if (calls) ++*calls;
        std::sort(first, last, less);
    }
};

}  // namespace

int main() {
    std::mt19937_64 rng(2026);

    {
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        std::vector<double> v(100000);
        for (auto& x : v) x = d(rng);
        check("double random", v);
        std::sort(v.begin(), v.end());
        check("double sorted", v);
        std::reverse(v.begin(), v.end());
        check("double reverse", v);
    }

    {
        std::vector<double> v(50000, 1.0);
        check("double equal", v);
    }

    {
        std::vector<double> v = {3.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 -0.0,
                                 0.0,
                                 -1.0,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()};
        check("double nan", v);
        oblivone::sort(v);
        if (!std::isnan(v.back())) {
            std::printf("FAIL NaN last\n");
            ++g_failures;
        }
    }

    {
        std::uniform_int_distribution<std::int32_t> d(
            (std::numeric_limits<std::int32_t>::min)(),
            (std::numeric_limits<std::int32_t>::max)());
        std::vector<std::int32_t> v(100000);
        for (auto& x : v) x = d(rng);
        check("int32 random", v);
    }

    {
        std::vector<std::int64_t> v = {
            (std::numeric_limits<std::int64_t>::min)(),
            (std::numeric_limits<std::int64_t>::max)(), 0, -1, 1};
        check("int64 extremes", v);
    }

    {
        std::vector<std::uint64_t> v = {
            0, 1, (std::numeric_limits<std::uint64_t>::max)()};
        check("uint64 extremes", v);
    }

    {
        std::vector<float> v = {
            3.0f, std::numeric_limits<float>::quiet_NaN(), -0.0f, 0.0f,
            -1.0f, std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()};
        check("float nan", v);
        oblivone::sort(v);
        if (!std::isnan(v.back())) {
            std::printf("FAIL float NaN last\n");
            ++g_failures;
        }
    }

    {
        oblivone::options opt;
        opt.small_top = 0;
        opt.small_sort = 0;
        opt.leaf_sort = 0;
        opt.scan_threshold = 0;
        opt.max_depth = 3;
        opt.bucket_limit = 1;
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        std::vector<double> v(5000);
        for (auto& x : v) x = d(rng);
        check("edge options", v, opt);
    }

    {
        std::vector<Item> v(20000);
        std::uniform_int_distribution<std::int32_t> d(-1000, 1000);
        for (auto& x : v) x = Item{d(rng), 0.0};
        oblivone::sort_by(v, [](const Item& x) {
            return static_cast<std::uint64_t>(
                       static_cast<std::int64_t>(x.key)) ^
                   (std::uint64_t(1) << 63);
        });
        for (std::size_t i = 1; i < v.size(); ++i) {
            if (v[i].key < v[i - 1].key) {
                std::printf("FAIL sort_by at %zu\n", i);
                ++g_failures;
                break;
            }
        }
    }

    {
        oblivone::options opt;
        opt.small_top = 10;
        int calls = 0;
        CountingFallback fallback{&calls};
        std::vector<double> values = {5.0, 4.0, 3.0, 2.0, 1.0};
        oblivone::sort(values, opt, fallback);
        if (calls == 0) {
            std::printf("FAIL custom fallback not called\n");
            ++g_failures;
        }
        if (!std::is_sorted(values.begin(), values.end())) {
            std::printf("FAIL custom fallback result\n");
            ++g_failures;
        }
    }

    {
        oblivone::options opt;
        opt.small_top = 2;
        std::vector<double> tiny = {2.0, 1.0};
        check("tiny", tiny, opt);
    }

    if (g_failures == 0) {
        std::printf("test_sort passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}

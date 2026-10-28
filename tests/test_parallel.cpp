#include <oblivone/parallel.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace {
int failures = 0;

bool less_double(double a, double b) {
    return oblivone::detail::less_double(a, b);
}

template <class T>
void verify(const char* name, std::vector<T> values, unsigned threads,
            const oblivone::options& opt = {}) {
    auto reference = values;
    std::sort(reference.begin(), reference.end(),
              [](const T& a, const T& b) {
                  return oblivone::detail::key_of(a) <
                         oblivone::detail::key_of(b);
              });

    oblivone::parallel_options popt;
    popt.threads = threads;
    popt.min_n = 1000;
    oblivone::parallel_sort(values, opt, popt);

    for (std::size_t i = 1; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) <
            oblivone::detail::key_of(values[i - 1])) {
            std::printf("FAIL order %s T=%u at %zu\n", name, threads, i);
            ++failures;
            return;
        }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) !=
            oblivone::detail::key_of(reference[i])) {
            std::printf("FAIL multiset %s T=%u at %zu\n", name, threads, i);
            ++failures;
            return;
        }
    }
}

struct Item {
    std::int64_t key;
};

void check_sort_by() {
    std::mt19937_64 rng(4242);
    std::uniform_int_distribution<std::int64_t> d(-100000, 100000);
    std::vector<Item> items(20000);
    for (auto& x : items) x = Item{d(rng)};

    auto reference = items;
    std::sort(reference.begin(), reference.end(),
              [](const Item& a, const Item& b) { return a.key < b.key; });

    oblivone::parallel_options popt;
    popt.threads = 4;
    popt.min_n = 1000;
    oblivone::parallel_sort_by(
        items, [](const Item& x) {
            return static_cast<std::uint64_t>(x.key) ^
                   (std::uint64_t(1) << 63);
        },
        oblivone::options{}, popt);

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].key != reference[i].key) {
            std::printf("FAIL sort_by at %zu\n", i);
            ++failures;
            return;
        }
    }
}

void check_pool_reuse() {
    oblivone::thread_pool pool(4);
    oblivone::parallel_options popt;
    popt.pool = &pool;
    popt.min_n = 1000;

    std::mt19937_64 rng(9191);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    for (int round = 0; round < 3; ++round) {
        std::vector<double> values(100000);
        for (auto& x : values) x = d(rng);
        oblivone::parallel_sort(values, oblivone::options{}, popt);
        if (!std::is_sorted(values.begin(), values.end(), less_double)) {
            std::printf("FAIL pool reuse round %d\n", round);
            ++failures;
            return;
        }
    }
}
}  // namespace

int main() {
    std::mt19937_64 rng(2026);
    std::uniform_real_distribution<double> d(-1.0, 1.0);

    std::vector<double> random(300000);
    for (auto& x : random) x = d(rng);
    verify("random", random, 2);
    verify("random", random, 4);

    oblivone::options cached;
    cached.cache_bucket_index = true;
    verify("random cache", random, 4, cached);

    auto sorted = random;
    std::sort(sorted.begin(), sorted.end());
    verify("sorted", sorted, 4);
    auto reverse = sorted;
    std::reverse(reverse.begin(), reverse.end());
    verify("reverse", reverse, 4);

    std::vector<double> few(200000);
    for (std::size_t i = 0; i < few.size(); ++i) {
        few[i] = static_cast<double>(i % 10);
    }
    verify("few", few, 4);

    std::vector<double> special{
        3.0,
        (std::numeric_limits<double>::quiet_NaN)(),
        -0.0,
        0.0,
        -1.0,
        (std::numeric_limits<double>::infinity)(),
        -(std::numeric_limits<double>::infinity)(),
        (std::numeric_limits<double>::quiet_NaN)()};
    verify("special", special, 2);

    check_sort_by();
    check_pool_reuse();

    if (failures == 0) {
        std::printf("test_parallel passed\n");
        return 0;
    }
    return 1;
}

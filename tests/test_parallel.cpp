// Parallel API correctness tests.

#include <oblivone/parallel.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int g_failures = 0;

template <class T>
bool sorted_by_key(const std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (oblivone::key_of(v[i]) < oblivone::key_of(v[i - 1])) return false;
    }
    return true;
}

template <class T>
void check(const char* name, std::vector<T> v, unsigned threads) {
    oblivone::parallel_options popt;
    popt.threads = threads;
    popt.min_n = 1000;
    oblivone::parallel_sort(v, oblivone::options{}, popt);
    if (!sorted_by_key(v)) {
        std::printf("FAIL %s threads=%u\n", name, threads);
        ++g_failures;
    }
}

struct Item {
    std::int32_t key;
    double value;
};

void test_items() {
    const std::size_t n = 200000;
    std::mt19937_64 rng(9);
    std::uniform_int_distribution<int> d(-1000, 1000);
    std::vector<Item> v(n);
    for (auto& x : v) x = Item{d(rng), 0.0};

    oblivone::parallel_options popt;
    popt.threads = 4;
    popt.min_n = 1000;
    oblivone::parallel_sort_by(
        v, [](const Item& x) {
            return static_cast<std::uint64_t>(
                       static_cast<std::int64_t>(x.key)) ^
                   0x8000000000000000ull;
        },
        oblivone::options{}, popt);

    if (!std::is_sorted(v.begin(), v.end(),
                        [](const Item& a, const Item& b) {
                            return a.key < b.key;
                        })) {
        std::printf("FAIL items\n");
        ++g_failures;
    }
}

}  // namespace

int main() {
    const std::size_t n = 300000;
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> d(-1.0, 1.0);

    std::vector<double> random(n);
    for (auto& x : random) x = d(rng);
    for (unsigned t : {1u, 2u, 4u}) check("random", random, t);

    auto sorted = random;
    std::sort(sorted.begin(), sorted.end());
    check("sorted", sorted, 4);

    auto reversed = sorted;
    std::reverse(reversed.begin(), reversed.end());
    check("reverse", reversed, 4);

    std::vector<double> equal(n, 1.0);
    check("equal", equal, 4);

    std::vector<double> small(17);
    for (auto& x : small) x = d(rng);
    check("small", small, 4);

    std::vector<double> empty;
    check("empty", empty, 4);

    {
        oblivone::thread_pool pool(2);
        oblivone::parallel_options popt;
        popt.pool = &pool;
        popt.min_n = 1000;
        auto v = random;
        oblivone::parallel_sort(v, oblivone::options{}, popt);
        if (!sorted_by_key(v)) {
            std::printf("FAIL injected pool\n");
            ++g_failures;
        }
    }

    test_items();

    if (g_failures == 0) {
        std::printf("parallel tests passed\n");
        return 0;
    }
    std::printf("%d parallel test(s) failed\n", g_failures);
    return 1;
}

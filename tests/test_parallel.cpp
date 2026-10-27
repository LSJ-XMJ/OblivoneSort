#include <oblivone/parallel.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

template <class T>
bool sorted_by_key(const std::vector<T>& values) {
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) <
            oblivone::detail::key_of(values[i - 1])) {
            return false;
        }
    }
    return true;
}

void check(const char* name, std::vector<double> values, unsigned threads) {
    oblivone::parallel_options popt;
    popt.threads = threads;
    popt.min_n = 1000;
    oblivone::parallel_sort(values, oblivone::options{}, popt);
    if (!sorted_by_key(values)) {
        std::printf("FAIL %s T=%u\n", name, threads);
        ++g_failures;
    }
}

struct Item {
    std::int32_t key;
    double value;
};

}  // namespace

int main() {
    const std::size_t n = 300000;
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    std::vector<double> random(n);
    for (auto& x : random) x = dist(rng);
    for (unsigned threads : {1u, 2u, 4u}) {
        check("random", random, threads);
    }

    auto sorted = random;
    std::sort(sorted.begin(), sorted.end());
    check("sorted", sorted, 4);

    auto reversed = sorted;
    std::reverse(reversed.begin(), reversed.end());
    check("reverse", reversed, 4);

    check("equal", std::vector<double>(n, 1.0), 4);
    check("small", std::vector<double>(17, 2.0), 4);
    check("empty", std::vector<double>(), 4);

    {
        oblivone::thread_pool pool(2);
        oblivone::parallel_options popt;
        popt.pool = &pool;
        popt.min_n = 1000;
        auto values = random;
        oblivone::parallel_sort(values, oblivone::options{}, popt);
        if (!sorted_by_key(values)) {
            std::printf("FAIL injected pool\n");
            ++g_failures;
        }
    }

    {
        oblivone::options opt;
        opt.cache_bucket_index = true;

        oblivone::parallel_options popt;
        popt.threads = 2;
        popt.min_n = 1000;
        popt.max_buckets = 65536;

        auto values = random;
        oblivone::parallel_sort(values, opt, popt);
        if (!sorted_by_key(values)) {
            std::printf("FAIL cached parallel u16\n");
            ++g_failures;
        }

        oblivone::thread_pool pool(2);
        popt.pool = &pool;
        auto injected = random;
        oblivone::parallel_sort(injected, opt, popt);
        if (!sorted_by_key(injected)) {
            std::printf("FAIL cached parallel injected\n");
            ++g_failures;
        }

        oblivone::options unlimited;
        unlimited.cache_bucket_index = true;
        unlimited.bucket_limit = (std::numeric_limits<std::size_t>::max)();
        popt.pool = nullptr;
        popt.max_buckets = 0;
        auto ignored = random;
        oblivone::parallel_sort(ignored, unlimited, popt);
        if (!sorted_by_key(ignored)) {
            std::printf("FAIL cached parallel >64K ignored\n");
            ++g_failures;
        }

        popt.min_n = 1000000;
        auto serial_fallback = std::vector<double>(random.begin(),
                                                   random.begin() + 50000);
        oblivone::parallel_sort(serial_fallback, opt, popt);
        if (!sorted_by_key(serial_fallback)) {
            std::printf("FAIL cached serial fallback\n");
            ++g_failures;
        }
    }

    {
        std::vector<Item> items(50000);
        std::uniform_int_distribution<std::int32_t> d(-1000, 1000);
        for (auto& item : items) item = Item{d(rng), 0.0};

        oblivone::parallel_options popt;
        popt.threads = 2;
        popt.min_n = 1000;
        oblivone::parallel_sort_by(items, [](const Item& item) {
            return static_cast<std::uint64_t>(
                       static_cast<std::int64_t>(item.key)) ^
                   (std::uint64_t(1) << 63);
        }, oblivone::options{}, popt);

        for (std::size_t i = 1; i < items.size(); ++i) {
            if (items[i].key < items[i - 1].key) {
                std::printf("FAIL parallel_sort_by\n");
                ++g_failures;
                break;
            }
        }
    }

    {
        std::atomic<int> failures{0};
        std::vector<std::thread> callers;
        for (int i = 0; i < 4; ++i) {
            callers.emplace_back([&, i] {
                std::mt19937_64 local_rng(100 + static_cast<std::uint64_t>(i));
                std::uniform_real_distribution<double> local_dist(-1.0, 1.0);
                std::vector<double> values(50000);
                for (auto& x : values) x = local_dist(local_rng);

                oblivone::parallel_options popt;
                popt.threads = 2;
                popt.min_n = 1000;
                oblivone::parallel_sort(values, oblivone::options{}, popt);
                if (!sorted_by_key(values)) ++failures;
            });
        }
        for (auto& caller : callers) caller.join();

        oblivone::thread_pool shared_pool(2);
        callers.clear();
        for (int i = 0; i < 2; ++i) {
            callers.emplace_back([&, i] {
                std::mt19937_64 local_rng(200 + static_cast<std::uint64_t>(i));
                std::uniform_real_distribution<double> local_dist(-1.0, 1.0);
                std::vector<double> values(50000);
                for (auto& x : values) x = local_dist(local_rng);

                oblivone::parallel_options popt;
                popt.pool = &shared_pool;
                popt.min_n = 1000;
                oblivone::parallel_sort(values, oblivone::options{}, popt);
                if (!sorted_by_key(values)) ++failures;
            });
        }
        for (auto& caller : callers) caller.join();

        if (failures.load() != 0) {
            std::printf("FAIL concurrent (%d)\n", failures.load());
            ++g_failures;
        }
    }

    {
        std::mt19937_64 small_rng(303);
        std::uniform_int_distribution<std::size_t> size_dist(0, 20000);
        std::uniform_real_distribution<double> value_dist(-1.0, 1.0);

        for (int iteration = 0; iteration < 40; ++iteration) {
            std::vector<double> values(size_dist(small_rng));
            for (auto& x : values) x = value_dist(small_rng);

            oblivone::parallel_options popt;
            popt.threads = 2;
            popt.min_n = 1000;
            oblivone::parallel_sort(values, oblivone::options{}, popt);
            if (!sorted_by_key(values)) {
                std::printf("FAIL small parallel iteration %d\n", iteration);
                ++g_failures;
                break;
            }
        }
    }

    if (g_failures == 0) {
        std::printf("test_parallel passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}

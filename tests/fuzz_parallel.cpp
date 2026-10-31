#include <oblivone/parallel.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {
int failures = 0;

bool key_less(double a, double b) {
    return oblivone::detail::key_of(a) < oblivone::detail::key_of(b);
}

void verify(std::vector<double> values, const oblivone::options& opt,
            const oblivone::parallel_options& popt) {
    auto reference = values;
    std::sort(reference.begin(), reference.end(), key_less);
    oblivone::parallel_sort(values, opt, popt);

    for (std::size_t i = 1; i < values.size(); ++i) {
        if (key_less(values[i], values[i - 1])) {
            std::printf("FAIL fuzz order n=%zu T=%u\n", values.size(),
                        popt.threads);
            ++failures;
            return;
        }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) !=
            oblivone::detail::key_of(reference[i])) {
            std::printf("FAIL fuzz multiset n=%zu at %zu\n", values.size(), i);
            ++failures;
            return;
        }
    }
}

struct item {
    std::int64_t key;
};

void verify_by(std::vector<item> values, const oblivone::options& opt,
               const oblivone::parallel_options& popt) {
    auto reference = values;
    std::sort(reference.begin(), reference.end(),
              [](const item& a, const item& b) { return a.key < b.key; });
    oblivone::parallel_sort_by(
        values, [](const item& x) { return oblivone::detail::key_of(x.key); },
        opt, popt);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].key != reference[i].key) {
            std::printf("FAIL fuzz sort_by n=%zu at %zu\n", values.size(), i);
            ++failures;
            return;
        }
    }
}
}  // namespace

int main(int argc, char** argv) {
    const std::uint64_t iterations =
        (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 2000;
    const std::uint64_t seed =
        (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1;

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::uniform_int_distribution<int> choose(0, 1000);
    std::uniform_int_distribution<std::int64_t> id(-1000000, 1000000);

    for (std::uint64_t it = 0; it < iterations; ++it) {
        const std::size_t n = static_cast<std::size_t>(choose(rng) % 40000);
        std::vector<double> values(n);
        for (auto& x : values) x = d(rng);

        const int pattern = choose(rng) % 6;
        if (pattern == 1) {
            std::sort(values.begin(), values.end(), key_less);
        } else if (pattern == 2) {
            std::sort(values.begin(), values.end(), key_less);
            std::reverse(values.begin(), values.end());
        } else if (pattern == 3) {
            for (std::size_t i = 0; i < n; ++i) {
                values[i] = static_cast<double>(i % 10);
            }
        } else if (pattern == 4) {
            for (auto& x : values) x = 1.0;
        } else if (pattern == 5 && n > 8) {
            values[0] = (std::numeric_limits<double>::quiet_NaN)();
            values[1] = -(std::numeric_limits<double>::infinity)();
            values[2] = (std::numeric_limits<double>::infinity)();
            values[3] = -0.0;
            values[4] = 0.0;
            values[8] = (std::numeric_limits<double>::quiet_NaN)();
        }

        oblivone::options opt;
        opt.small_top = static_cast<std::size_t>(choose(rng) % 129);
        opt.small_sort = static_cast<std::size_t>(choose(rng) % 25);
        opt.leaf_sort = static_cast<std::size_t>(choose(rng) % 33);
        opt.scan_threshold = static_cast<std::size_t>(choose(rng) % 513);
        opt.max_depth = 1 + choose(rng) % 12;
        opt.bucket_limit = static_cast<std::size_t>(choose(rng) % 4097);
        opt.cache_bucket_index = (choose(rng) % 2) == 0;

        oblivone::parallel_options popt;
        popt.threads = 1 + static_cast<unsigned>(choose(rng) % 8);
        popt.min_n = 1000 + static_cast<std::size_t>(choose(rng) % 5000);
        popt.max_buckets = static_cast<std::size_t>(choose(rng) % 65537);
        popt.coarse_buckets = static_cast<std::size_t>(choose(rng) % 1033);
        popt.local_bucket_limit =
            static_cast<std::size_t>(choose(rng) % 4100);
        if ((choose(rng) % 4) == 0) {
            popt.scratch_budget =
                1024 + static_cast<std::size_t>(choose(rng) % 1000000);
        }

        verify(std::move(values), opt, popt);
        if (failures) return 1;

        if ((it % 3) == 0) {
            std::vector<item> items(n);
            for (auto& x : items) x.key = id(rng);
            verify_by(std::move(items), opt, popt);
            if (failures) return 1;
        }
    }

    std::printf("fuzz_parallel passed: %llu cases, seed=%llu\n",
                static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed));
    return 0;
}

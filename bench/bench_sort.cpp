// Copyright 2026 肖明杰 (Xiao Mingjie)
// SPDX-License-Identifier: Apache-2.0
//
// Minimal benchmark: std::sort vs oblivone::sort on several distributions.
// Not a rigorous benchmark harness (no pinning, no statistics); it is meant
// to catch regressions and to give a first order-of-magnitude picture.

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

std::vector<double> make_uniform(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> make_normal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> make_exponential(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::exponential_distribution<double> d(1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> make_pareto(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = std::pow(u(rng), -1.0 / 1.5);
    return v;
}

std::vector<double> make_bimodal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> a(0.0, 0.05), b(10.0, 0.05);
    std::bernoulli_distribution pick(0.5);
    std::vector<double> v(n);
    for (auto& x : v) x = pick(rng) ? a(rng) : b(rng);
    return v;
}

std::vector<double> make_few_values(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, 9);
    std::vector<double> v(n);
    for (auto& x : v) x = static_cast<double>(d(rng));
    return v;
}

void run_case(const char* name, const std::vector<double>& base, std::size_t n,
              std::size_t max_buckets) {
    std::vector<double> a = base;
    std::vector<double> b = base;
    oblivone::options opt;
    opt.max_buckets = max_buckets;

    double t_std = 1e300;
    double t_x = 1e300;
    for (int r = 0; r < 3; ++r) {
        a = base;  // reset outside the timed region
        auto t0 = clock_type::now();
        std::sort(a.begin(), a.end());
        auto t1 = clock_type::now();
        t_std = (std::min)(t_std, std::chrono::duration<double, std::milli>(t1 - t0).count());

        b = base;
        t0 = clock_type::now();
        oblivone::sort(b.data(), b.size(), opt);
        t1 = clock_type::now();
        t_x = (std::min)(t_x, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    if (a != b) {
        std::printf("%-12s n=%-9zu MISMATCH\n", name, n);
        return;
    }
    const double ns_std = t_std * 1e6 / static_cast<double>(n);
    const double ns_x = t_x * 1e6 / static_cast<double>(n);
    std::printf("%-12s n=%-9zu std::sort %8.1f ns/elem   oblivone %8.1f ns/elem   %5.2fx\n",
                name, n, ns_std, ns_x, ns_std / ns_x);
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n = (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 1000000;
    const std::size_t max_buckets = (argc > 2) ? static_cast<std::size_t>(std::atoll(argv[2]))
                                               : (std::size_t(1) << 20);

    std::printf("n=%zu, max_buckets=%zu\n", n, max_buckets);

    run_case("uniform", make_uniform(n, 1), n, max_buckets);
    run_case("normal", make_normal(n, 2), n, max_buckets);
    run_case("exponential", make_exponential(n, 3), n, max_buckets);
    run_case("pareto", make_pareto(n, 4), n, max_buckets);
    run_case("bimodal", make_bimodal(n, 5), n, max_buckets);
    run_case("few_values", make_few_values(n, 6), n, max_buckets);

    auto sorted = make_uniform(n, 7);
    std::sort(sorted.begin(), sorted.end());
    run_case("sorted", sorted, n, max_buckets);

    auto rev = sorted;
    std::reverse(rev.begin(), rev.end());
    run_case("reverse", rev, n, max_buckets);

    return 0;
}

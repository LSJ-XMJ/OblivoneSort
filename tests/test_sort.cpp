// Copyright 2026 肖明杰 (Xiao Mingjie)
// SPDX-License-Identifier: Apache-2.0
//
// 对拍测试：每个用例都用同一套键序与 std::sort 逐元素比较。

#include <oblivone/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

template <class T>
std::uint64_t key_of(const T& v) {
    return oblivone::key_of(v);
}

template <class T>
bool is_sorted_by_key(const std::vector<T>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (key_of(v[i - 1]) > key_of(v[i])) return false;
    }
    return true;
}

template <class T>
void check(const char* name, std::vector<T> data, oblivone::options opt = {}) {
    std::vector<T> expect = data;
    std::sort(expect.begin(), expect.end(),
              [](const T& a, const T& b) { return key_of(a) < key_of(b); });

    oblivone::sort(data.data(), data.size(), opt);

    if (!is_sorted_by_key(data)) {
        std::printf("FAIL sorted: %s\n", name);
        ++g_failures;
        return;
    }
    if (data.size() != expect.size()) {
        std::printf("FAIL size: %s\n", name);
        ++g_failures;
        return;
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (key_of(data[i]) != key_of(expect[i])) {
            std::printf("FAIL mismatch at %zu: %s\n", i, name);
            ++g_failures;
            return;
        }
    }
}

std::vector<double> uniform(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> normal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> exponential(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::exponential_distribution<double> d(1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

std::vector<double> pareto(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = std::pow(u(rng), -1.0 / 1.5);  // 帕累托 α = 1.5
    return v;
}

std::vector<double> bimodal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> a(0.0, 0.05), b(10.0, 0.05);
    std::bernoulli_distribution pick(0.5);
    std::vector<double> v(n);
    for (auto& x : v) x = pick(rng) ? a(rng) : b(rng);
    return v;
}

std::vector<double> few_values(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, 9);
    std::vector<double> v(n);
    for (auto& x : v) x = static_cast<double>(d(rng));
    return v;
}

// 单离群点 + 窄区间主体：最容易让“估位置”类算法失手的构造。
std::vector<double> outlier(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.0, 1e-3);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    v.back() = 1e9;
    return v;
}

// 分形：64 个尺度，每簇是 2^-k 附近的窄带。
std::vector<double> fractal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        const int k = static_cast<int>(i % 64);
        const double base = std::pow(2.0, -k);
        v[i] = base * (1.0 + 1e-9 * u(rng));
    }
    return v;
}

// 近重复：互不相同但极其接近，随机打乱。
std::vector<double> near_duplicates(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 1.0 + static_cast<double>(i) * 1e-12 + 1e-15 * u(rng);
    }
    std::shuffle(v.begin(), v.end(), rng);  // 打乱，避免走“已排序”快速路径
    return v;
}

void test_size_sweep() {
    for (std::size_t n : {0u, 1u, 2u, 3u, 17u, 63u, 64u, 65u, 257u, 1000u, 10000u}) {
        const std::string tag = "uniform(n=" + std::to_string(n) + ")";
        check(tag.c_str(), uniform(n, 12345 + n));
    }
}

void test_distributions() {
    const std::size_t n = 100000;
    check("normal", normal(n, 1));
    check("exponential", exponential(n, 2));
    check("pareto", pareto(n, 3));
    check("bimodal", bimodal(n, 4));
    check("few_values", few_values(n, 5));
}

void test_orders() {
    auto v = uniform(50000, 7);
    auto sorted = v;
    std::sort(sorted.begin(), sorted.end());
    check("already_sorted", sorted);

    auto rev = sorted;
    std::reverse(rev.begin(), rev.end());
    check("reverse", rev);

    auto nearly = sorted;
    std::mt19937_64 rng(8);
    std::uniform_int_distribution<std::size_t> idx(0, nearly.size() - 1);
    for (int i = 0; i < 100; ++i) std::swap(nearly[idx(rng)], nearly[idx(rng)]);
    check("nearly_sorted", nearly);
}

void test_duplicates() {
    check("all_equal", std::vector<double>(10000, 3.14));
    std::vector<double> two(100000);
    for (std::size_t i = 0; i < two.size(); ++i) two[i] = (i & 1) ? 1.0 : 0.0;
    check("two_values", two);
}

void test_float_extremes() {
    std::vector<double> v = {
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        0.0, -0.0, 1.0, -1.0,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::quiet_NaN(),
        3.5, -2.5,
    };
    check("float_extremes", v);

    std::vector<double> big = uniform(100000, 9);
    big.push_back(std::numeric_limits<double>::quiet_NaN());
    big.push_back(std::numeric_limits<double>::infinity());
    big.push_back(-std::numeric_limits<double>::infinity());
    check("nan_tail", big);
}

void test_int64() {
    std::vector<std::int64_t> v = {
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max(),
        0, -1, 1, -2, 2,
        4611686018427387904LL, -4611686018427387904LL,
    };
    check("int64_extremes", v);

    std::mt19937_64 rng(10);
    std::uniform_int_distribution<std::int64_t> d(
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max());
    std::vector<std::int64_t> big(200000);
    for (auto& x : big) x = d(rng);
    check("int64_random", big);
}

void test_options() {
    auto v = normal(50000, 11);
    oblivone::options small;
    small.small_sort = 8;
    check("small_sort=8", v, small);

    oblivone::options shallow;
    shallow.max_depth = 1;
    check("max_depth=1", v, shallow);

    oblivone::options capped;
    capped.max_buckets = 4;
    check("max_buckets=4", v, capped);

    // 退化参数：0 必须按 1 处理，不能下溢；1 也要能正确收敛。
    oblivone::options zero;
    zero.max_buckets = 0;
    check("max_buckets=0", v, zero);

    oblivone::options one;
    one.max_buckets = 1;
    check("max_buckets=1", v, one);
}

struct record {
    double key;
    int id;
};

void test_sort_by() {
    std::mt19937_64 rng(12);
    std::normal_distribution<double> d(0, 1);
    std::vector<record> v(20000);
    for (int i = 0; i < static_cast<int>(v.size()); ++i) v[i] = {d(rng), i};

    auto expect = v;
    std::sort(expect.begin(), expect.end(),
              [](const record& a, const record& b) { return a.key < b.key; });

    oblivone::sort_by(v, [](const record& r) { return oblivone::key_of(r.key); });

    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i].key != expect[i].key) {
            std::printf("FAIL sort_by at %zu\n", i);
            ++g_failures;
            return;
        }
    }
}

void test_adversarial() {
    const std::size_t n = 200000;
    check("outlier", outlier(n, 21));
    check("fractal", fractal(n, 22));
    check("near_duplicates", near_duplicates(n, 23));

    // 99.9% 在 [0, 1]，0.1% 落在很远的尾部
    std::mt19937_64 rng(24);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> tail(n);
    for (std::size_t i = 0; i < n; ++i) {
        tail[i] = (i % 1000 == 0) ? 1e9 + d(rng) : d(rng);
    }
    check("heavy_tail", tail);

    // 强制提前走兜底，结果仍然必须正确
    oblivone::options shallow;
    shallow.max_depth = 1;
    check("fractal_depth1", fractal(n, 25), shallow);
}

void test_xsort_entries() {
    auto v = normal(20000, 31);
    oblivone::xsort(v);
    if (!is_sorted_by_key(v)) {
        std::printf("FAIL xsort entry\n");
        ++g_failures;
    }

    auto w = normal(20000, 32);
    oblivone::xsort_by(w, [](const double& x) { return oblivone::key_of(x); });
    if (!is_sorted_by_key(w)) {
        std::printf("FAIL xsort_by entry\n");
        ++g_failures;
    }
}

struct counting_fallback {
    int* calls;
    int* elems;
    template <class T, class Cmp>
    void operator()(T* first, T* last, Cmp comp) const {
        ++*calls;
        *elems += static_cast<int>(last - first);
        std::sort(first, last, comp);
    }
};

void test_fallback_injection() {
    auto v = normal(50000, 13);
    int calls = 0;
    int elems = 0;

    // small_sort=16、leaf_sort=22 时，块 <22 插入，22+ 继续进核心，不一定触发兜底；
    // 把深度压到 1，强制子块走 fallback，才能验证注入。
    oblivone::options opt;
    opt.max_depth = 1;
    oblivone::sort(v.data(), v.size(), opt,
                   counting_fallback{&calls, &elems});
    if (!is_sorted_by_key(v) || calls <= 0) {
        std::printf("FAIL fallback injection\n");
        ++g_failures;
        return;
    }
    std::printf("  fallback: %d calls, %d elements\n", calls, elems);

    auto w = normal(20000, 14);
    oblivone::oblivone_sort(w.data(), w.size(),
                            [](const double& x) { return oblivone::key_of(x); });
    if (!is_sorted_by_key(w)) {
        std::printf("FAIL oblivone_sort core entry\n");
        ++g_failures;
    }
}

void test_small_blocks_insertion() {
    // 默认 small_sort=16、leaf_sort=22：n>16 进 Oblivone；核心内 <22 的组用插入排序，
    // 22+ 才继续 Oblivone，所以正常数据不应触发 fallback。
    auto v = normal(20000, 31);
    int calls = 0;
    int elems = 0;
    oblivone::sort(v.data(), v.size(), oblivone::options{},
                   counting_fallback{&calls, &elems});
    if (!is_sorted_by_key(v) || calls != 0) {
        std::printf("FAIL small blocks use insertion (calls=%d)\n", calls);
        ++g_failures;
        return;
    }
    std::printf("  small blocks: 0 fallback calls\n");
}

void test_pure_oblivone() {
    // leaf_sort=0, small_sort=0：完全不插入，小块也继续 Oblivone；
    // 靠深度兜底收敛。
    auto v = uniform(5000, 41);
    oblivone::options opt;
    opt.small_sort = 0;
    opt.leaf_sort = 0;
    oblivone::sort(v.data(), v.size(), opt);
    if (!is_sorted_by_key(v)) {
        std::printf("FAIL pure oblivone path\n");
        ++g_failures;
        return;
    }
    std::printf("  pure oblivone: ok\n");
}

}  // namespace

int main() {
    test_size_sweep();
    test_distributions();
    test_orders();
    test_duplicates();
    test_float_extremes();
    test_int64();
    test_options();
    test_sort_by();
    test_fallback_injection();
    test_small_blocks_insertion();
    test_pure_oblivone();
    test_adversarial();
    test_xsort_entries();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failures);
    return 1;
}

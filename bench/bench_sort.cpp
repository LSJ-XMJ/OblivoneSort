// Copyright 2026 肖明杰 (Xiao Mingjie)
// SPDX-License-Identifier: Apache-2.0
//
// 基准：std::sort、LSD 基数排序、oblivone::sort 的对比。
//
// pdqsort 和 Boost.Sort spreadsort 用 __has_include 自动检测：
// 把 pdqsort.h 放进 third_party/，或安装 Boost，就会自动加入对比；
// 都没有时只跑前三者，保持仓库零依赖。
//
// 这是一个“冒烟/回归”基准，不是发表级基准：取 3 次最好值，不绑核，
// 不做统计检验，只用来发现退化、给出量级。

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#if defined(__has_include)
#  if __has_include(<pdqsort.h>)
#    include <pdqsort.h>
#    define BENCH_HAS_PDQSORT 1
#  endif
#  if __has_include(<boost/sort/spreadsort/spreadsort.hpp>)
#    include <boost/sort/spreadsort/spreadsort.hpp>
#    define BENCH_HAS_SPREADSORT 1
#  endif
#endif

namespace {

using clock_type = std::chrono::steady_clock;

// ---------------------------------------------------------------- 数据生成

// 均匀分布 [0, 1)。
std::vector<double> make_uniform(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

// 标准正态。
std::vector<double> make_normal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> d(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

// 指数分布（λ=1）。
std::vector<double> make_exponential(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::exponential_distribution<double> d(1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    return v;
}

// 帕累托分布（α=1.5）：重尾。
std::vector<double> make_pareto(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = std::pow(u(rng), -1.0 / 1.5);
    return v;
}

// 双峰：两个很窄的正态峰。
std::vector<double> make_bimodal(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> a(0.0, 0.05), b(10.0, 0.05);
    std::bernoulli_distribution pick(0.5);
    std::vector<double> v(n);
    for (auto& x : v) x = pick(rng) ? a(rng) : b(rng);
    return v;
}

// 少值：0~9，大量重复。
std::vector<double> make_few_values(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> d(0, 9);
    std::vector<double> v(n);
    for (auto& x : v) x = static_cast<double>(d(rng));
    return v;
}

// 单离群点 + 窄区间主体：最容易让“估位置”类算法失手的构造。
std::vector<double> make_outlier(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(0.0, 1e-3);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    v.back() = 1e9;
    return v;
}

// 分形：64 个尺度，每簇是 2^-k 附近的窄带。
std::vector<double> make_fractal(std::size_t n, std::uint64_t seed) {
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

// 近重复：互不相同但极其接近，随机打乱（避免被“已排序”快速路径接住）。
std::vector<double> make_near_duplicates(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 1.0 + static_cast<double>(i) * 1e-12 + 1e-15 * u(rng);
    }
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// 几乎有序：先排好，再随机做少量“相邻交换”。
// 相邻交换每次只制造 O(1) 个逆序对，插入排序的代价是 O(n + 交换数)。
std::vector<double> make_almost_sorted(std::size_t n, std::uint64_t seed,
                                       double fraction) {
    auto v = make_uniform(n, seed);
    std::sort(v.begin(), v.end());
    std::mt19937_64 rng(seed ^ 0x9e3779b97f4a7c15ull);
    std::uniform_int_distribution<std::size_t> idx(0, n - 2);
    const std::size_t swaps = static_cast<std::size_t>(static_cast<double>(n) * fraction);
    for (std::size_t k = 0; k < swaps; ++k) {
        const std::size_t i = idx(rng);
        std::swap(v[i], v[i + 1]);
    }
    return v;
}

// organ pipe：升序前半 + 降序后半。
std::vector<double> make_organ_pipe(std::size_t n, std::uint64_t seed) {
    auto v = make_uniform(n, seed);
    std::sort(v.begin(), v.end());
    std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    return v;
}

// 插入排序基线：几乎有序时 Cost = n + 逆序对数。
void insertion_sort_range(double* a, std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) {
        const double x = a[i];
        std::size_t j = i;
        while (j > 0 && x < a[j - 1]) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = x;
    }
}

void insertion_sort_all(std::vector<double>& v) {
    insertion_sort_range(v.data(), v.size());
}

// 块内随机、块间有序：块 b 的值都落在 [b, b+1)。
std::vector<double> make_block_shuffled(std::size_t n, std::size_t k,
                                        std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<double> v(n);
    std::size_t pos = 0;
    for (; pos + k <= n; pos += k) {
        const double block = static_cast<double>(pos / k);
        for (std::size_t j = 0; j < k; ++j) v[pos + j] = block + u(rng);
    }
    const double block = static_cast<double>(pos / k);
    for (std::size_t j = pos; j < n; ++j) v[j] = block + u(rng);
    return v;
}

// ------------------------------------------------------------------ 算法

// 对保序 64 位键做 LSD 基数排序。
// 分布排序最直接的对手：固定 8 趟、每趟 8 位，O(8n)，额外 O(n) 内存。
void radix_sort(std::vector<double>& v) {
    struct item {
        std::uint64_t key;
        double value;
    };
    std::vector<item> a(v.size());
    std::vector<item> b(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        a[i] = item{oblivone::key_of(v[i]), v[i]};
    }

    constexpr int BITS = 8;
    constexpr std::size_t RADIX = std::size_t(1) << BITS;
    constexpr std::uint64_t MASK = RADIX - 1;

    for (int shift = 0; shift < 64; shift += BITS) {
        std::size_t cnt[RADIX] = {};
        for (const auto& e : a) ++cnt[(e.key >> shift) & MASK];

        std::size_t sum = 0;
        for (std::size_t i = 0; i < RADIX; ++i) {
            const std::size_t c = cnt[i];
            cnt[i] = sum;
            sum += c;
        }
        for (const auto& e : a) b[cnt[(e.key >> shift) & MASK]++] = e;
        a.swap(b);
    }

    for (std::size_t i = 0; i < v.size(); ++i) v[i] = a[i].value;
}

// ------------------------------------------------------------------ 计时

// 在 base 的副本上跑 fn，重复 reps 次；复制不计入计时，返回最快一次（毫秒）。
template <class F>
double best_ms(const std::vector<double>& base, F fn, int reps = 3) {
    std::vector<double> work;
    double best = 1e300;
    for (int r = 0; r < reps; ++r) {
        work = base;
        const auto t0 = clock_type::now();
        fn(work);
        const auto t1 = clock_type::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        best = (std::min)(best, ms);
    }
    return best;
}

// 跑一次并校验结果是否与预期（std::sort 的结果）一致。
template <class F>
bool produces(const std::vector<double>& base,
              const std::vector<double>& expected, F fn) {
    std::vector<double> work = base;
    fn(work);
    return work == expected;
}

// ------------------------------------------------------------------ 输出

void run_case(const char* name, const std::vector<double>& base, std::size_t n,
              std::size_t max_buckets) {
    std::vector<double> expected = base;
    std::sort(expected.begin(), expected.end());

    const auto std_sort = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
    };
    const auto radix = [](std::vector<double>& v) { radix_sort(v); };
    oblivone::options opt;
    opt.max_buckets = max_buckets;
    const auto oblivone_sort = [&](std::vector<double>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };

    // 先校验正确性，再计时；任何一个算法出错就直接报 MISMATCH。
    if (!produces(base, expected, std_sort) ||
        !produces(base, expected, radix) ||
        !produces(base, expected, oblivone_sort)) {
        std::printf("%-12s n=%-9zu MISMATCH\n", name, n);
        return;
    }

    const double t_std = best_ms(base, std_sort);
    const double t_radix = best_ms(base, radix);
    const double t_x = best_ms(base, oblivone_sort);

#if defined(BENCH_HAS_PDQSORT)
    const auto pdq = [](std::vector<double>& v) { pdqsort(v.begin(), v.end()); };
    const double t_pdq = best_ms(base, pdq);
#endif
#if defined(BENCH_HAS_SPREADSORT)
    const auto spread = [](std::vector<double>& v) {
        boost::sort::spreadsort::float_sort(v.begin(), v.end());
    };
    const double t_spread = best_ms(base, spread);
#endif

    const double scale = 1e6 / static_cast<double>(n);
    std::printf("%-12s n=%-9zu std %6.1f  radix %6.1f  oblivone %6.1f",
                name, n, t_std * scale, t_radix * scale, t_x * scale);
#if defined(BENCH_HAS_PDQSORT)
    std::printf("  pdq %6.1f", t_pdq * scale);
#endif
#if defined(BENCH_HAS_SPREADSORT)
    std::printf("  spread %6.1f", t_spread * scale);
#endif
    std::printf("  | vs std %5.2fx\n", t_std / t_x);
}

// 几乎有序场景：std::sort、插入排序、oblivone 三者对比。
void run_adaptive_case(const char* name, const std::vector<double>& base,
                       std::size_t n, std::size_t max_buckets) {
    std::vector<double> expected = base;
    std::sort(expected.begin(), expected.end());

    const auto std_sort = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
    };
    const auto insertion = [](std::vector<double>& v) { insertion_sort_all(v); };
    oblivone::options opt;
    opt.max_buckets = max_buckets;
    const auto oblivone_sort = [&](std::vector<double>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };

    if (!produces(base, expected, std_sort) ||
        !produces(base, expected, insertion) ||
        !produces(base, expected, oblivone_sort)) {
        std::printf("%-16s n=%-9zu MISMATCH\n", name, n);
        return;
    }

    const double t_std = best_ms(base, std_sort);
    const double t_ins = best_ms(base, insertion);
    const double t_x = best_ms(base, oblivone_sort);

    const double scale = 1e6 / static_cast<double>(n);
    std::printf("%-16s n=%-9zu std %6.1f  insertion %6.1f  oblivone %6.1f"
                "  | insertion %5.2fx std, %5.2fx faster than oblivone\n",
                name, n, t_std * scale, t_ins * scale, t_x * scale,
                t_std / t_ins, t_x / t_ins);
}

// 块内随机、块间有序：全量插入 vs 逐块插入。
void run_block_case(std::size_t k, std::size_t n) {
    auto base = make_block_shuffled(n, k, 100 + k);
    auto expected = base;
    std::sort(expected.begin(), expected.end());

    const auto global_ins = [](std::vector<double>& v) { insertion_sort_all(v); };
    const auto leaf_ins = [k](std::vector<double>& v) {
        std::size_t pos = 0;
        for (; pos + k <= v.size(); pos += k) {
            insertion_sort_range(v.data() + pos, k);
        }
        if (pos < v.size()) insertion_sort_range(v.data() + pos, v.size() - pos);
    };

    if (!produces(base, expected, global_ins) ||
        !produces(base, expected, leaf_ins)) {
        std::printf("block k=%-3zu MISMATCH\n", k);
        return;
    }

    const double t_g = best_ms(base, global_ins);
    const double t_l = best_ms(base, leaf_ins);
    const double scale = 1e6 / static_cast<double>(n);
    std::printf("block k=%-3zu n=%-9zu global %6.1f  leaf %6.1f  | leaf/global %5.2fx\n",
                k, n, t_g * scale, t_l * scale, t_g / t_l);
}

// 三档交叉点：叶子插入 vs 快速排序 vs Oblivone 核心，按块大小 k 测。
// 每个块独立随机，blocks = n / k，三个算法各排一遍所有块。
void run_tier_case(std::size_t k, std::size_t n) {
    const std::size_t blocks = n / k;
    std::mt19937_64 rng(7000 + k);
    std::uniform_real_distribution<double> d(0.0, 1.0);
    std::vector<double> base(blocks * k);
    for (auto& x : base) x = d(rng);

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    oblivone::options opt;
    opt.small_sort = 1;  // 强制核心自己分桶，不要在入口就 fallback
    opt.max_depth = 16;
    opt.max_buckets = 4096;
    std::vector<double> buf(k);

    const auto leaf = [&](std::vector<double>& v) {
        for (std::size_t b = 0; b < blocks; ++b) {
            double* p = v.data() + b * k;
            for (std::size_t i = 1; i < k; ++i) {
                const double x = p[i];
                std::size_t j = i;
                while (j > 0 && key(x) < key(p[j - 1])) {
                    p[j] = p[j - 1];
                    --j;
                }
                p[j] = x;
            }
        }
    };
    const auto quick = [&](std::vector<double>& v) {
        for (std::size_t b = 0; b < blocks; ++b) {
            std::sort(v.begin() + static_cast<std::ptrdiff_t>(b * k),
                      v.begin() + static_cast<std::ptrdiff_t>((b + 1) * k),
                      [&](double x, double y) { return key(x) < key(y); });
        }
    };
    const auto oblivone = [&](std::vector<double>& v) {
        for (std::size_t b = 0; b < blocks; ++b) {
            oblivone::detail::sort_range(v.data() + b * k, k, 0, key,
                                         oblivone::default_fallback{}, opt, buf);
        }
    };

    // 每个块独立排序，所以期望值是“逐块排好”，不是全量排序。
    auto expected = base;
    for (std::size_t b = 0; b < blocks; ++b) {
        std::sort(expected.begin() + static_cast<std::ptrdiff_t>(b * k),
                  expected.begin() + static_cast<std::ptrdiff_t>((b + 1) * k));
    }
    if (!produces(base, expected, leaf) || !produces(base, expected, quick) ||
        !produces(base, expected, oblivone)) {
        std::printf("k=%-4zu MISMATCH\n", k);
        return;
    }

    const double t_leaf = best_ms(base, leaf, 5);
    const double t_quick = best_ms(base, quick, 5);
    const double t_ob = best_ms(base, oblivone, 5);
    const double scale = 1e6 / static_cast<double>(blocks * k);

    const char* best = "leaf";
    double best_t = t_leaf;
    if (t_quick < best_t) { best = "quick"; best_t = t_quick; }
    if (t_ob < best_t) { best = "oblivone"; best_t = t_ob; }

    std::printf("k=%-4zu leaf %6.1f  quick %6.1f  oblivone %6.1f  | best %s\n",
                k, t_leaf * scale, t_quick * scale, t_ob * scale, best);
}

// 调 small_sort：同一批数据，比较 16 和 64 两档。
void run_small_sort_probe(std::size_t n, std::size_t max_buckets) {
    struct item {
        const char* name;
        std::vector<double> data;
    };
    std::vector<item> sets;
    sets.push_back({"uniform", make_uniform(n, 123)});
    sets.push_back({"normal", make_normal(n, 124)});
    sets.push_back({"exponential", make_exponential(n, 125)});
    sets.push_back({"bimodal", make_bimodal(n, 126)});

    for (std::size_t ss : {std::size_t(16), std::size_t(64)}) {
        std::printf("small_sort=%zu:", ss);
        for (auto& s : sets) {
            auto expected = s.data;
            std::sort(expected.begin(), expected.end());
            oblivone::options opt;
            opt.small_sort = ss;
            opt.max_buckets = max_buckets;
            const auto f = [&](std::vector<double>& x) {
                oblivone::sort(x.data(), x.size(), opt);
            };
            if (!produces(s.data, expected, f)) {
                std::printf(" %s=MISMATCH", s.name);
                continue;
            }
            const double t = best_ms(s.data, f);
            std::printf(" %s=%5.1f", s.name,
                        t * 1e6 / static_cast<double>(n));
        }
        std::printf("  (ns/elem)\n");
    }
}

// 叶子阈值扫描：令 small_sort = leaf_sort = T，测端到端时间。
// 这样块 <T 用插入，≥T 继续 Oblivone，不会被中间的 fallback 档干扰。
void run_leaf_probe(std::size_t n, std::size_t max_buckets) {
    struct item {
        const char* name;
        std::vector<double> data;
    };
    std::vector<item> sets;
    sets.push_back({"uniform", make_uniform(n, 301)});
    sets.push_back({"normal", make_normal(n, 302)});
    sets.push_back({"exponential", make_exponential(n, 303)});
    sets.push_back({"few", make_few_values(n, 304)});
    sets.push_back({"organ", make_organ_pipe(n, 305)});

    std::printf("leaf");
    for (auto& s : sets) std::printf(" %s", s.name);
    std::printf("  (ns/elem)\n");

    for (std::size_t T : {std::size_t(4), std::size_t(8), std::size_t(12),
                          std::size_t(16), std::size_t(24), std::size_t(32),
                          std::size_t(48), std::size_t(64)}) {
        std::printf("%zu", T);
        for (auto& s : sets) {
            oblivone::options opt;
            opt.small_sort = T;
            opt.leaf_sort = T;
            opt.max_buckets = max_buckets;
            const auto f = [&](std::vector<double>& x) {
                oblivone::sort(x.data(), x.size(), opt);
            };
            auto expected = s.data;
            std::sort(expected.begin(), expected.end());
            if (!produces(s.data, expected, f)) {
                std::printf(" MISMATCH");
                continue;
            }
            const double t = best_ms(s.data, f);
            std::printf(" %6.1f", t * 1e6 / static_cast<double>(n));
        }
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 1000000;
    const std::size_t max_buckets =
        (argc > 2) ? static_cast<std::size_t>(std::atoll(argv[2]))
                   : (std::size_t(1) << 20);

    std::printf("n=%zu, max_buckets=%zu\n", n, max_buckets);
#if !defined(BENCH_HAS_PDQSORT) && !defined(BENCH_HAS_SPREADSORT)
    std::printf("(未找到 pdqsort / spreadsort；见 bench/README.md)\n");
#endif

    run_case("uniform", make_uniform(n, 1), n, max_buckets);
    run_case("normal", make_normal(n, 2), n, max_buckets);
    run_case("exponential", make_exponential(n, 3), n, max_buckets);
    run_case("pareto", make_pareto(n, 4), n, max_buckets);
    run_case("bimodal", make_bimodal(n, 5), n, max_buckets);
    run_case("few_values", make_few_values(n, 6), n, max_buckets);
    run_case("outlier", make_outlier(n, 8), n, max_buckets);
    run_case("fractal", make_fractal(n, 9), n, max_buckets);
    run_case("near_dup", make_near_duplicates(n, 10), n, max_buckets);

    auto sorted = make_uniform(n, 7);
    std::sort(sorted.begin(), sorted.end());
    run_case("sorted", sorted, n, max_buckets);

    auto rev = sorted;
    std::reverse(rev.begin(), rev.end());
    run_case("reverse", rev, n, max_buckets);

    run_adaptive_case("sorted+0.1%", make_almost_sorted(n, 11, 0.001), n, max_buckets);
    run_adaptive_case("sorted+1%", make_almost_sorted(n, 12, 0.01), n, max_buckets);
    run_adaptive_case("sorted+5%", make_almost_sorted(n, 13, 0.05), n, max_buckets);

    // 阈值/三档探针只在小规模下跑；n 很大时跳过，避免跑太久。
    if (n <= 1000000) {
        for (std::size_t k : {std::size_t(2), std::size_t(4), std::size_t(8),
                              std::size_t(16)}) {
            run_block_case(k, n);
        }

        for (std::size_t k : {std::size_t(4), std::size_t(8), std::size_t(12),
                              std::size_t(16), std::size_t(24), std::size_t(32),
                              std::size_t(48), std::size_t(64), std::size_t(96),
                              std::size_t(128), std::size_t(192), std::size_t(256)}) {
            run_tier_case(k, n);
        }

        run_small_sort_probe(n, max_buckets);
        run_leaf_probe(n, max_buckets);
    }

    return 0;
}

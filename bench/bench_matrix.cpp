// 覆盖矩阵基准：类型 × 分布 × 规模。
// 目的不是刷分，而是看“哪些数据赢、哪些输”，再决定阈值。
//
// 用法：
//   bench_matrix [n] [reps]
// 输出 CSV，方便贴到表格里。

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum Dist {
    D_UNIFORM,
    D_NORMAL,
    D_EXPONENTIAL,
    D_PARETO,
    D_BIMODAL,
    D_FEW,
    D_SORTED,
    D_REVERSE,
    D_ORGAN,
    D_SAWTOOTH,
    D_RUNS,
    D_EQUAL,
    D_NEAR_DUP,
};

const char* dist_name(int d) {
    switch (d) {
        case D_UNIFORM: return "uniform";
        case D_NORMAL: return "normal";
        case D_EXPONENTIAL: return "exponential";
        case D_PARETO: return "pareto";
        case D_BIMODAL: return "bimodal";
        case D_FEW: return "few";
        case D_SORTED: return "sorted";
        case D_REVERSE: return "reverse";
        case D_ORGAN: return "organ";
        case D_SAWTOOTH: return "sawtooth";
        case D_RUNS: return "runs";
        case D_EQUAL: return "equal";
        case D_NEAR_DUP: return "near_dup";
    }
    return "?";
}

// ---------- 每类型的均匀取值 ----------

template <class T>
T uniform_value(std::mt19937_64& rng) {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        return static_cast<T>(d(rng));
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        std::uniform_int_distribution<std::int32_t> d(-1000000000, 1000000000);
        return d(rng);
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        std::uniform_int_distribution<long long> d(-1000000000000000000LL,
                                                  1000000000000000000LL);
        return static_cast<T>(d(rng));
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        std::uniform_int_distribution<std::uint32_t> d(0u, 4000000000u);
        return d(rng);
    } else {
        std::uniform_int_distribution<unsigned long long> d(
            0ull, 1000000000000000000ull);
        return static_cast<T>(d(rng));
    }
}

// ---------- 浮点分布 ----------

std::vector<double> make_double(int dist, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<double> v(n);

    switch (dist) {
        case D_UNIFORM: {
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_NORMAL: {
            std::normal_distribution<double> d(0.0, 1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_EXPONENTIAL: {
            std::exponential_distribution<double> d(1.0);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_PARETO: {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            for (auto& x : v) x = std::pow(1.0 - u(rng), -1.0 / 1.5);
            break;
        }
        case D_BIMODAL: {
            std::normal_distribution<double> a(-4.0, 1.0), b(4.0, 1.0);
            for (std::size_t i = 0; i < n; ++i) v[i] = (i & 1) ? a(rng) : b(rng);
            break;
        }
        case D_FEW: {
            std::uniform_int_distribution<int> d(0, 9);
            for (auto& x : v) x = d(rng);
            break;
        }
        case D_SORTED:
        case D_REVERSE: {
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& x : v) x = d(rng);
            std::sort(v.begin(), v.end());
            if (dist == D_REVERSE) std::reverse(v.begin(), v.end());
            break;
        }
        case D_ORGAN: {
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (auto& x : v) x = d(rng);
            std::sort(v.begin(), v.end());
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
            break;
        }
        case D_SAWTOOTH: {
            const std::size_t period = 64;
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<double>(i % period);
            }
            break;
        }
        case D_RUNS: {
            const std::size_t run = 64;
            std::uniform_real_distribution<double> d(-1.0, 1.0);
            for (std::size_t i = 0; i < n; i += run) {
                const std::size_t end = (std::min)(i + run, n);
                for (std::size_t j = i; j < end; ++j) v[j] = d(rng);
                std::sort(v.begin() + static_cast<std::ptrdiff_t>(i),
                          v.begin() + static_cast<std::ptrdiff_t>(end));
            }
            break;
        }
        case D_EQUAL: {
            for (auto& x : v) x = 1.0;
            break;
        }
        case D_NEAR_DUP: {
            std::uniform_real_distribution<double> d(-1e-15, 1e-15);
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = 1.0 + static_cast<double>(i) * 1e-12 + d(rng);
            }
            std::shuffle(v.begin(), v.end(), rng);
            break;
        }
        default:
            break;
    }
    return v;
}

// ---------- 通用 LSD 基数排序（8 趟）----------

template <class T>
void radix_sort(std::vector<T>& v) {
    const std::size_t n = v.size();
    if (n < 2) return;

    std::vector<std::uint64_t> keys(n), tmp_keys(n);
    std::vector<T> vals(n), tmp_vals(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys[i] = oblivone::key_of(v[i]);
        vals[i] = v[i];
    }

    for (int pass = 0; pass < 8; ++pass) {
        std::size_t count[256] = {};
        const int shift = pass * 8;
        for (std::size_t i = 0; i < n; ++i) {
            ++count[(keys[i] >> shift) & 0xFFu];
        }
        std::size_t sum = 0;
        for (std::size_t b = 0; b < 256; ++b) {
            const std::size_t c = count[b];
            count[b] = sum;
            sum += c;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t b = (keys[i] >> shift) & 0xFFu;
            const std::size_t pos = count[b]++;
            tmp_keys[pos] = keys[i];
            tmp_vals[pos] = vals[i];
        }
        keys.swap(tmp_keys);
        vals.swap(tmp_vals);
    }
    v.swap(vals);
}

// ---------- 计时 ----------

template <class T, class F>
double median_ms(const std::vector<T>& base, F f, int reps) {
    std::vector<double> t;
    t.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        t.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (!std::is_sorted(v.begin(), v.end())) {
            std::printf("MISMATCH\n");
            std::exit(2);
        }
    }
    std::sort(t.begin(), t.end());
    return t[static_cast<std::size_t>(reps / 2)];
}

template <class T>
void run_one(const char* type, int dist, std::vector<T> base, std::size_t n,
             int reps, std::size_t small_sort) {
    const auto std_sort = [](std::vector<T>& v) { std::sort(v.begin(), v.end()); };
    const auto radix = [](std::vector<T>& v) { radix_sort(v); };
    oblivone::options opt;
    opt.small_sort = small_sort;
    const auto oblivone = [&](std::vector<T>& v) {
        oblivone::sort(v.data(), v.size(), opt);
    };

    const double t_std = median_ms(base, std_sort, reps);
    const double t_radix = median_ms(base, radix, reps);
    const double t_ob = median_ms(base, oblivone, reps);

    const double scale = 1e6 / static_cast<double>(n);
    std::printf("%s,%s,%zu,%.1f,%.1f,%.1f,%.2f\n", type, dist_name(dist), n,
                t_std * scale, t_radix * scale, t_ob * scale, t_std / t_ob);
}

template <class T>
void run_type_matrix(const char* type, std::size_t n, int reps,
                      std::size_t small_sort) {
    std::mt19937_64 rng(12345);
    std::vector<T> uniform(n);
    for (auto& x : uniform) x = uniform_value<T>(rng);

    run_one(type, D_UNIFORM, uniform, n, reps, small_sort);

    auto sorted = uniform;
    std::sort(sorted.begin(), sorted.end());
    run_one(type, D_SORTED, sorted, n, reps, small_sort);

    auto reverse = sorted;
    std::reverse(reverse.begin(), reverse.end());
    run_one(type, D_REVERSE, reverse, n, reps, small_sort);

    auto few = uniform;
    for (std::size_t i = 0; i < n; ++i) few[i] = static_cast<T>(i % 10);
    run_one(type, D_FEW, few, n, reps, small_sort);
}

void run_double_matrix(std::size_t n, int reps, std::size_t small_sort) {
    for (int d = D_UNIFORM; d <= D_NEAR_DUP; ++d) {
        run_one("double", d, make_double(d, n, 1000 + static_cast<std::uint64_t>(d)),
                n, reps, small_sort);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 200000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t small_sort =
        (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                   : 16;

    std::printf("type,dist,n,std_ns,radix_ns,oblivone_ns,speedup\n");

    run_type_matrix<std::int32_t>("int32", n, reps, small_sort);
    run_type_matrix<std::int64_t>("int64", n, reps, small_sort);
    run_type_matrix<std::uint32_t>("uint32", n, reps, small_sort);
    run_type_matrix<std::uint64_t>("uint64", n, reps, small_sort);
    run_type_matrix<float>("float", n, reps, small_sort);
    run_double_matrix(n, reps, small_sort);

    return 0;
}

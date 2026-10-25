// 二维网格：长度 k=2..128 × 叶子阈值 T=2..64。
// 每个 (k, T, 分布) 用 1000 组，比较 插入 / quick / Oblivone(T)。
// 输出：
//   1) 明细 CSV：k,dist,T,insertion_ns,quick_ns,oblivone_ns,winner
//   2) 每个 k 的最优 T 和胜者
//   3) 每种分布的胜者计数与连续区间
//
// 用法：bench_grid [groups，默认 1000]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum Dist { D_UNIFORM, D_NORMAL, D_EXPONENTIAL, D_FEW };

const char* dist_name(int d) {
    switch (d) {
        case D_UNIFORM: return "uniform";
        case D_NORMAL: return "normal";
        case D_EXPONENTIAL: return "exponential";
        case D_FEW: return "few";
    }
    return "?";
}

std::vector<double> make_groups(int dist, std::size_t k, std::size_t groups,
                                std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<double> v(groups * k);
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
        case D_FEW: {
            std::uniform_int_distribution<int> d(0, 9);
            for (auto& x : v) x = d(rng);
            break;
        }
    }
    return v;
}

template <class F>
double avg_ns_per_group(const std::vector<double>& base, std::size_t k,
                        std::size_t groups, F f) {
    const auto key = [](const double& x) { return oblivone::key_of(x); };
    auto v = base;
    const auto t0 = Clock::now();
    f(v);
    const auto t1 = Clock::now();

    for (std::size_t off = 0; off + k <= v.size(); off += k) {
        for (std::size_t i = 1; i < k; ++i) {
            if (key(v[off + i]) < key(v[off + i - 1])) {
                std::printf("MISMATCH k=%zu\n", k);
                std::exit(2);
            }
        }
    }
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / static_cast<double>(groups);
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t groups =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000;
    const auto key = [](const double& x) { return oblivone::key_of(x); };

    std::printf("k,dist,T,insertion_ns,quick_ns,oblivone_ns,winner\n");

    for (int dist = D_UNIFORM; dist <= D_FEW; ++dist) {
        long long row_ins = 0, row_quick = 0, row_ob = 0;

        std::vector<int> best_t(129, 0);
        std::vector<const char*> best_winner(129, "insertion");
        std::vector<double> best_time(129, 0.0);
        std::vector<double> ins_time(129, 0.0);
        std::vector<double> quick_time(129, 0.0);

        for (std::size_t k = 2; k <= 128; ++k) {
            const auto base = make_groups(
                dist, k, groups,
                100000 + static_cast<std::uint64_t>(dist) * 1000 + k);

            const auto insertion = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off + k <= v.size(); off += k) {
                    oblivone::detail::insertion_sort(v.data() + off, k, key);
                }
            };
            const auto quick = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off + k <= v.size(); off += k) {
                    std::sort(v.begin() + static_cast<std::ptrdiff_t>(off),
                              v.begin() + static_cast<std::ptrdiff_t>(off + k),
                              [&](double x, double y) { return key(x) < key(y); });
                }
            };

            const double t_ins = avg_ns_per_group(base, k, groups, insertion);
            const double t_quick = avg_ns_per_group(base, k, groups, quick);
            ins_time[k] = t_ins;
            quick_time[k] = t_quick;

            double best = (std::min)(t_ins, t_quick);
            const char* best_name = (t_ins <= t_quick) ? "insertion" : "quick";

            std::vector<double> buf(k);
            for (std::size_t T = 2; T <= 64 && T <= k; ++T) {
                oblivone::options opt;
                opt.small_sort = T;
                opt.leaf_sort = T;
                const auto oblivone = [&](std::vector<double>& v) {
                    for (std::size_t off = 0; off + k <= v.size(); off += k) {
                        oblivone::detail::sort_range(
                            v.data() + off, k, 0, key,
                            oblivone::default_fallback{}, opt, buf);
                    }
                };
                const double t_ob =
                    avg_ns_per_group(base, k, groups, oblivone);

                const char* row_winner =
                    (t_ob < (std::min)(t_ins, t_quick))
                        ? "oblivone"
                        : ((t_ins <= t_quick) ? "insertion" : "quick");
                if (std::string(row_winner) == "oblivone") ++row_ob;
                else if (std::string(row_winner) == "insertion") ++row_ins;
                else ++row_quick;

                std::printf("%zu,%s,%zu,%.1f,%.1f,%.1f,%s\n", k,
                            dist_name(dist), T, t_ins, t_quick, t_ob, row_winner);

                if (t_ob < best) {
                    best = t_ob;
                    best_name = "oblivone";
                    best_t[k] = static_cast<int>(T);
                }
            }

            best_time[k] = best;
            best_winner[k] = best_name;
            std::printf("# best k=%zu dist=%s T=%d ins=%.1f quick=%.1f "
                        "best=%.1f winner=%s\n",
                        k, dist_name(dist), best_t[k], t_ins, t_quick, best,
                        best_name);
        }

        // 连续区间
        std::printf("# ranges dist=%s:", dist_name(dist));
        std::size_t start = 2;
        const char* cur = best_winner[2];
        for (std::size_t k = 3; k <= 128; ++k) {
            if (std::string(best_winner[k]) != cur) {
                std::printf(" %zu..%zu=%s", start, k - 1, cur);
                start = k;
                cur = best_winner[k];
            }
        }
        std::printf(" %zu..128=%s\n", start, cur);

        std::printf("# counts dist=%s: insertion=%lld quick=%lld oblivone=%lld "
                    "total=%lld\n",
                    dist_name(dist), row_ins, row_quick, row_ob,
                    row_ins + row_quick + row_ob);
    }
    return 0;
}

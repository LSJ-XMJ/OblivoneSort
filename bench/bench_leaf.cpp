// n=2..16K：插入 vs 快速排序 vs Oblivone+叶子。
// 尺寸按 2..16 步 1、32..128 步 16、256..1024 步 128、
// 2048..4096 步 1024、8192..16384 步 4096；每个 n × 分布 1000 组。
// 插入 n>1024 标 -1（O(n^2)）。
//
// 注意：只有“插入 vs Oblivone”这一列才回答叶子阈值；
// “插入 vs quick”只回答叶子里该用哪种排序。
//
// 用法：bench_leaf [reps，默认 5]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kGroups = 1000;

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

std::vector<double> make_groups(int dist, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<double> v(kGroups * n);
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

std::vector<std::size_t> make_sizes() {
    std::vector<std::size_t> s;
    for (std::size_t n = 2; n <= 16; ++n) s.push_back(n);
    for (std::size_t n = 32; n <= 128; n += 16) s.push_back(n);
    for (std::size_t n = 256; n <= 1024; n += 128) s.push_back(n);
    for (std::size_t n = 2048; n <= 4096; n += 1024) s.push_back(n);
    for (std::size_t n = 8192; n <= 16384; n += 4096) s.push_back(n);
    return s;
}

template <class F>
double avg_ns_per_group(const std::vector<double>& base, std::size_t n, F f,
                        int reps) {
    const auto key = [](const double& x) { return oblivone::key_of(x); };
    double sum = 0.0;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();

        for (std::size_t off = 0; off + n <= v.size(); off += n) {
            for (std::size_t i = 1; i < n; ++i) {
                if (key(v[off + i]) < key(v[off + i - 1])) {
                    std::printf("MISMATCH n=%zu\n", n);
                    std::exit(2);
                }
            }
        }
        const double ns =
            std::chrono::duration<double, std::nano>(t1 - t0).count();
        sum += ns / static_cast<double>(kGroups);
    }
    return sum / static_cast<double>(reps);
}

}  // namespace

int main(int argc, char** argv) {
    const int reps = (argc > 1) ? std::atoi(argv[1]) : 5;
    const auto key = [](const double& x) { return oblivone::key_of(x); };

    std::printf("n,dist,insertion_ns,quick_ns,oblivone_ns,"
                "ratio_ins_over_oblivone,winner\n");

    for (int dist = D_UNIFORM; dist <= D_FEW; ++dist) {
        double sum_ins = 0.0;
        double sum_quick = 0.0;
        double sum_ob = 0.0;
        int count_ins = 0;
        int count_all = 0;

        for (const std::size_t n : make_sizes()) {
            const auto base =
                make_groups(dist, n, 9000 + static_cast<std::uint64_t>(dist) * 100 + n);

            const auto insertion = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off + n <= v.size(); off += n) {
                    oblivone::detail::insertion_sort(v.data() + off, n, key);
                }
            };
            const auto quick = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off + n <= v.size(); off += n) {
                    std::sort(v.begin() + static_cast<std::ptrdiff_t>(off),
                              v.begin() + static_cast<std::ptrdiff_t>(off + n),
                              [&](double x, double y) { return key(x) < key(y); });
                }
            };

            oblivone::options ob_opt;
            ob_opt.small_sort = 0;   // 不让调度层兜底
            ob_opt.leaf_sort = 16;   // 小块用插入，模拟当前默认
            std::vector<double> buf(n);
            const auto oblivone = [&](std::vector<double>& v) {
                for (std::size_t off = 0; off + n <= v.size(); off += n) {
                    oblivone::detail::sort_range(
                        v.data() + off, n, 0, key, oblivone::default_fallback{},
                        ob_opt, buf);
                }
            };

            const bool do_ins = (n <= 1024);  // 再大插入是 O(n^2)，不测
            const double t_ins =
                do_ins ? avg_ns_per_group(base, n, insertion, reps) : -1.0;
            const double t_quick = avg_ns_per_group(base, n, quick, reps);
            const double t_ob = avg_ns_per_group(base, n, oblivone, reps);

            sum_quick += t_quick;
            sum_ob += t_ob;
            if (do_ins) {
                sum_ins += t_ins;
                ++count_ins;
            }
            ++count_all;

            const char* winner = "quick";
            double best = t_quick;
            if (t_ob < best) { winner = "oblivone"; best = t_ob; }
            if (do_ins && t_ins < best) { winner = "insertion"; }

            if (do_ins) {
                std::printf("%zu,%s,%.1f,%.1f,%.1f,%.3f,%s\n", n,
                            dist_name(dist), t_ins, t_quick, t_ob, t_ins / t_ob,
                            winner);
            } else {
                std::printf("%zu,%s,-1,%.1f,%.1f,-1,%s\n", n, dist_name(dist),
                            t_quick, t_ob, winner);
            }
        }

        std::printf("# %s avg (sampled): insertion %.1f (%d sizes), quick %.1f, "
                    "oblivone %.1f (%d sizes)\n",
                    dist_name(dist),
                    count_ins ? sum_ins / count_ins : 0.0, count_ins,
                    sum_quick / count_all, sum_ob / count_all, count_all);
    }
    return 0;
}

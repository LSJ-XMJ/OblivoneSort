// 小 n 对比：插入 / std::sort / Oblivone+插入 / 纯 Oblivone。
// 尺寸序列按要求分段：
//   2..16 步长 1；32..128 步长 16；256..1024 步长 128；
//   2048..4096 步长 1024；8192..16384 步长 4096；32768..65536 步长 16384。
// 插入排序在 n>1024 时是 O(n^2)，直接标 -1。
//
// 用法：bench_small [总元素数，默认 1000000]

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

template <class F>
double median_ms(const std::vector<double>& base, std::size_t n, F f,
                 int reps = 3) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

        for (std::size_t off = 0; off + n <= v.size(); off += n) {
            for (std::size_t i = 1; i < n; ++i) {
                if (oblivone::key_of(v[off + i]) <
                    oblivone::key_of(v[off + i - 1])) {
                    std::printf("MISMATCH n=%zu\n", n);
                    std::exit(2);
                }
            }
        }
    }
    std::sort(times.begin(), times.end());
    return times[static_cast<std::size_t>(reps / 2)];
}

std::vector<std::size_t> make_sizes() {
    std::vector<std::size_t> s;
    for (std::size_t n = 2; n <= 16; ++n) s.push_back(n);
    for (std::size_t n = 32; n <= 128; n += 16) s.push_back(n);
    for (std::size_t n = 256; n <= 1024; n += 128) s.push_back(n);
    for (std::size_t n = 2048; n <= 4096; n += 1024) s.push_back(n);
    for (std::size_t n = 8192; n <= 16384; n += 4096) s.push_back(n);
    for (std::size_t n = 32768; n <= 65536; n += 16384) s.push_back(n);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t total =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    std::printf("n,blocks,insertion_ns,std_ns,oblivone_ns,pure_ns,best\n");

    for (const std::size_t n : make_sizes()) {
        const std::size_t blocks = (std::max)(std::size_t(1), total / n);
        std::mt19937_64 rng(1000 + n);
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        std::vector<double> base(blocks * n);
        for (auto& x : base) x = d(rng);

        oblivone::options with_leaf;
        with_leaf.small_sort = 0;
        with_leaf.leaf_sort = 16;

        oblivone::options pure;
        pure.small_sort = 0;
        pure.leaf_sort = 0;

        std::vector<double> buf(n);

        const auto insertion = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += n) {
                oblivone::detail::insertion_sort(v.data() + off, n, key);
            }
        };
        const auto std_sort = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += n) {
                std::sort(v.begin() + static_cast<std::ptrdiff_t>(off),
                          v.begin() + static_cast<std::ptrdiff_t>(off + n),
                          [&](double x, double y) { return key(x) < key(y); });
            }
        };
        const auto oblivone_leaf = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += n) {
                oblivone::detail::sort_range(
                    v.data() + off, n, 0, key, oblivone::default_fallback{},
                    with_leaf, buf);
            }
        };
        const auto pure_oblivone = [&](std::vector<double>& v) {
            for (std::size_t off = 0; off < v.size(); off += n) {
                oblivone::detail::sort_range(
                    v.data() + off, n, 0, key, oblivone::default_fallback{},
                    pure, buf);
            }
        };

        const bool do_insertion = (n <= 1024);
        const double t_ins =
            do_insertion ? median_ms(base, n, insertion) : -1.0;
        const double t_std = median_ms(base, n, std_sort);
        const double t_ob = median_ms(base, n, oblivone_leaf);
        const double t_pure = median_ms(base, n, pure_oblivone);

        const double scale = 1e6 / static_cast<double>(blocks);

        const char* best = "std";
        double best_t = t_std;
        if (t_ob < best_t) { best = "oblivone"; best_t = t_ob; }
        if (t_pure < best_t) { best = "pure"; best_t = t_pure; }
        if (do_insertion && t_ins < best_t) { best = "insertion"; }

        if (do_insertion) {
            std::printf("%zu,%zu,%.1f,%.1f,%.1f,%.1f,%s\n", n, blocks,
                        t_ins * scale, t_std * scale, t_ob * scale,
                        t_pure * scale, best);
        } else {
            std::printf("%zu,%zu,-1,%.1f,%.1f,%.1f,%s\n", n, blocks,
                        t_std * scale, t_ob * scale, t_pure * scale, best);
        }
    }
    return 0;
}

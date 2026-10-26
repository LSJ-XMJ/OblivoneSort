// Prototype: 4 sub-buckets, 4 offset tables whose total size is exactly n.
// No global cnt[B]; per-sub-bucket offset tables are concatenated into one
// array of n counters, indexed by base[b] + offset.
//
// Usage: bench_offset_tables [n] [reps] [MT]

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

std::uint64_t keyd(double x) { return oblivone::key_of(x); }

bool less_d(double a, double b) { return keyd(a) < keyd(b); }

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(5000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (p == 1) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (p == 2) {
        std::sort(v.begin(), v.end());
        std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    }
    return v;
}

void offset_sort(double* a, std::size_t n, std::size_t MT) {
    if (n < 2) return;
    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    auto sub_of = [&](std::uint64_t k) -> std::size_t {
        const double u = static_cast<double>(k - mn) / static_cast<double>(range);
        std::size_t b = static_cast<std::size_t>(u * static_cast<double>(MT));
        return b >= MT ? MT - 1 : b;
    };

    // 4 张 offset 表的“基址”：就是每个子桶的起点。
    std::vector<std::uint32_t> base(MT + 1, 0);
    std::vector<std::uint32_t> c(MT, 0);
    for (std::size_t i = 0; i < n; ++i) ++c[sub_of(keyd(a[i]))];
    for (std::size_t b = 0; b < MT; ++b) base[b + 1] = base[b] + c[b];

    // 4 张 offset 表拼成一个长度 n 的数组。
    std::vector<std::uint32_t> off(n, 0);

    auto offset_of = [&](std::uint64_t k, std::size_t b) -> std::size_t {
        const double u = static_cast<double>(k - mn) / static_cast<double>(range);
        const double local = u * static_cast<double>(MT) - static_cast<double>(b);
        double t = local * static_cast<double>(c[b]);
        if (!(t > 0.0)) t = 0.0;
        std::size_t o = static_cast<std::size_t>(t);
        return o >= c[b] ? c[b] - 1 : o;
    };

    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        const std::size_t b = sub_of(k);
        ++off[base[b] + offset_of(k, b)];
    }

    // 每张子表独立前缀和，直接存绝对起点。
    for (std::size_t b = 0; b < MT; ++b) {
        std::uint32_t running = 0;
        for (std::size_t o = 0; o < c[b]; ++o) {
            const std::uint32_t cnt = off[base[b] + o];
            off[base[b] + o] = base[b] + running;
            running += cnt;
        }
    }

    std::vector<double> buf(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        const std::size_t b = sub_of(k);
        const std::size_t o = offset_of(k, b);
        const std::uint32_t pos = off[base[b] + o]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.end(), a);

    // 每个 offset 槽内再排；槽通常很小。
    for (std::size_t b = 0; b < MT; ++b) {
        for (std::size_t o = 0; o < c[b]; ++o) {
            const std::uint32_t end = off[base[b] + o];
            const std::uint32_t begin =
                (o == 0) ? base[b] : off[base[b] + o - 1];
            if (end - begin > 1) {
                std::sort(a + begin, a + end, less_d);
            }
        }
    }
}

template <class F>
double median_ms(const std::vector<double>& base, F f, int reps) {
    std::vector<double> times;
    for (int r = 0; r < reps; ++r) {
        auto v = base;
        const auto t0 = Clock::now();
        f(v);
        const auto t1 = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t MT =
        (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                   : 4;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    oblivone::options opt;

    std::printf("pattern,n,MT,base_ms,offset_ms,ratio\n");
    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);
        const auto base_sort = [&](std::vector<double>& v) {
            oblivone::oblivone_sort(v.data(), v.size(), key, opt);
        };
        const auto off_sort = [&](std::vector<double>& v) {
            offset_sort(v.data(), v.size(), MT);
        };

        {
            auto a = data; base_sort(a);
            auto b = data; off_sort(b);
            if (!std::is_sorted(a.begin(), a.end(), less_d) ||
                !std::is_sorted(b.begin(), b.end(), less_d)) {
                std::printf("MISMATCH\n");
                return 2;
            }
        }

        const double tb = median_ms(data, base_sort, reps);
        const double ts = median_ms(data, off_sort, reps);
        std::printf("%s,%zu,%zu,%.3f,%.3f,%.3f\n",
                    p == 0 ? "random" : (p == 1 ? "few" : "organ"), n, MT, tb, ts,
                    ts / tb);
    }
    return 0;
}

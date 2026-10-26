// Prototype: hierarchical sub-buckets.
// If a segment has more than CMax elements, split it into MT sub-buckets;
// otherwise use the normal adaptive bucket count.
//
// Usage: bench_sub_bucket [n] [reps] [CMax] [MT]

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

struct sbopt {
    std::size_t CMax = 4096;
    std::size_t MT = 16;
    int adaptive = 0;  // 0=equal width, 1=sample quantile, 2=histogram quantile
    std::size_t sample_size = 1024;
    std::size_t hist_bins = 1024;
    std::size_t small_sort = 16;
    std::size_t leaf_sort = 22;
    int max_depth = 16;
    std::size_t max_buckets = std::size_t(1) << 20;
};

enum Pattern { P_RANDOM, P_FEW, P_ORGAN };

const char* pattern_name(int p) {
    switch (p) {
        case P_RANDOM: return "random";
        case P_FEW: return "few";
        case P_ORGAN: return "organ";
    }
    return "?";
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(4000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);
    if (p == P_FEW) {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
    } else if (p == P_ORGAN) {
        std::sort(v.begin(), v.end());
        std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    }
    return v;
}

std::uint64_t keyd(double x) { return oblivone::key_of(x); }

void insertion_less(double* a, std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) {
        double v = a[i];
        const std::uint64_t kv = keyd(v);
        std::size_t j = i;
        while (j > 0 && kv < keyd(a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

void sort3_key(double* a) {
    std::uint64_t k0 = keyd(a[0]);
    std::uint64_t k1 = keyd(a[1]);
    std::uint64_t k2 = keyd(a[2]);
    if (k1 < k0) { std::swap(a[0], a[1]); std::swap(k0, k1); }
    if (k2 < k1) { std::swap(a[1], a[2]); std::swap(k1, k2); }
    if (k1 < k0) { std::swap(a[0], a[1]); }
}

// 等数量：用等距样本的分位数做边界，子桶目标数量≈m/MT。
std::vector<std::uint64_t> sample_bounds(const double* a, std::size_t m,
                                         std::size_t MT, std::size_t S) {
    std::vector<std::uint64_t> sample;
    const std::size_t stride = (std::max)(std::size_t(1), m / S);
    sample.reserve(m / stride + 1);
    for (std::size_t i = 0; i < m; i += stride) {
        sample.push_back(keyd(a[i]));
    }
    std::sort(sample.begin(), sample.end());

    std::vector<std::uint64_t> bounds;
    bounds.reserve(MT - 1);
    for (std::size_t j = 1; j < MT; ++j) {
        std::size_t idx = (j * sample.size()) / MT;
        if (idx >= sample.size()) idx = sample.size() - 1;
        const std::uint64_t k = sample[idx];
        if (bounds.empty() || k > bounds.back()) bounds.push_back(k);
    }
    return bounds;
}

void sb_core(double* a, std::size_t m, int depth, const sbopt& o,
             std::vector<double>& buf) {
    if (m < 2) return;
    if (m < o.small_sort || depth >= o.max_depth) {
        std::sort(a, a + m, [](double x, double y) {
            return oblivone::key_of(x) < oblivone::key_of(y);
        });
        return;
    }

    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < m; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;

    const std::size_t auto_cap =
        (m <= 2000000) ? (std::size_t(1) << 16) : (std::size_t(1) << 20);
    std::vector<std::uint64_t> bounds;
    std::size_t B;
    if (m > o.CMax && o.adaptive) {
        bounds = sample_bounds(a, m, o.MT, o.sample_size);
        B = bounds.size() + 1;
        if (B < 2) {
            B = 2;
            bounds.push_back(mx);
        }
    } else if (m > o.CMax) {
        B = o.MT;
    } else {
        B = (std::min)(m, (std::min)(o.max_buckets, auto_cap));
    }
    B = (std::max)(std::size_t(1), B);

    const std::uint64_t range = mx - mn;
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double t = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(t);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };
    auto sub_of = [&](std::uint64_t k) -> std::size_t {
        if (bounds.empty()) return bucket_of(k);
        return static_cast<std::size_t>(
            std::upper_bound(bounds.begin(), bounds.end(), k) - bounds.begin());
    };

    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++cnt[sub_of(keyd(a[i]))];
    }

    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }

    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = sub_of(keyd(a[i]));
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);

    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len <= 1) continue;
        if (len < o.leaf_sort) {
            if (len == 3) {
                sort3_key(a + begin);
            } else {
                insertion_less(a + begin, len);
            }
        } else if (len < o.small_sort) {
            std::sort(a + begin, a + end, [](double x, double y) {
                return oblivone::key_of(x) < oblivone::key_of(y);
            });
        } else {
            sb_core(a + begin, len, depth + 1, o, buf);
        }
    }
}

void sb_sort(double* a, std::size_t n, const sbopt& o) {
    if (n < 2) return;
    std::vector<double> buf(n);
    sb_core(a, n, 0, o, buf);
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

bool sorted_d(const std::vector<double>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (keyd(v[i]) < keyd(v[i - 1])) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    sbopt o;
    if (argc > 3) o.CMax = static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
    if (argc > 4) o.MT = static_cast<std::size_t>(std::strtoull(argv[4], nullptr, 10));
    if (argc > 5) o.adaptive = std::atoi(argv[5]) != 0;

    oblivone::options base;
    const auto key = [](const double& x) { return oblivone::key_of(x); };

    std::printf("pattern,n,CMax,MT,adaptive,base_ms,sub_ms,ratio\n");
    for (int p = P_RANDOM; p <= P_ORGAN; ++p) {
        const auto data = make_pattern(p, n);
        const auto base_sort = [&](std::vector<double>& v) {
            oblivone::oblivone_sort(v.data(), v.size(), key, base);
        };
        const auto sub_sort = [&](std::vector<double>& v) {
            sb_sort(v.data(), v.size(), o);
        };

        {
            auto a = data; base_sort(a);
            auto b = data; sub_sort(b);
            if (!sorted_d(a) || !sorted_d(b)) {
                std::printf("%s MISMATCH\n", pattern_name(p));
                return 2;
            }
        }

        const double tb = median_ms(data, base_sort, reps);
        const double ts = median_ms(data, sub_sort, reps);
        std::printf("%s,%zu,%zu,%zu,%d,%.3f,%.3f,%.3f\n", pattern_name(p), n,
                    o.CMax, o.MT, o.adaptive ? 1 : 0, tb, ts, ts / tb);
    }
    return 0;
}

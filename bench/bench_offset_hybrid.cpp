// Hybrid: m <= 8192 -> original core; m > 8192 -> 4/16 sub-buckets +
// offset tables (total m). Offset groups > 8192 recurse; otherwise original
// core. Depth limit falls back to the original core.
//
// Usage: bench_offset_hybrid [n] [reps] [MT]

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

constexpr std::size_t kOriginalMax = 8192;

std::uint64_t keyd(double x) { return oblivone::key_of(x); }
bool less_d(double a, double b) { return keyd(a) < keyd(b); }

enum Pattern {
    P_RANDOM,
    P_SORTED,
    P_REVERSE,
    P_ORGAN,
    P_SAWTOOTH,
    P_RUNS,
    P_EQUAL,
    P_FEW,
    P_NEAR_01,
    P_NEAR_1,
    P_NEAR_5,
    P_PUSH_FRONT,
    P_PUSH_BACK,
};

const char* pattern_name(int p) {
    switch (p) {
        case P_RANDOM: return "random";
        case P_SORTED: return "sorted";
        case P_REVERSE: return "reverse";
        case P_ORGAN: return "organ";
        case P_SAWTOOTH: return "sawtooth";
        case P_RUNS: return "runs";
        case P_EQUAL: return "equal";
        case P_FEW: return "few";
        case P_NEAR_01: return "near_0.1%";
        case P_NEAR_1: return "near_1%";
        case P_NEAR_5: return "near_5%";
        case P_PUSH_FRONT: return "push_front";
        case P_PUSH_BACK: return "push_back";
    }
    return "?";
}

std::vector<double> make_pattern(int p, std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) x = d(rng);

    switch (p) {
        case P_RANDOM: break;
        case P_SORTED: std::sort(v.begin(), v.end()); break;
        case P_REVERSE:
            std::sort(v.begin(), v.end());
            std::reverse(v.begin(), v.end());
            break;
        case P_ORGAN:
            std::sort(v.begin(), v.end());
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
            break;
        case P_SAWTOOTH:
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 64);
            break;
        case P_RUNS: {
            const std::size_t run = 64;
            for (std::size_t i = 0; i < n; i += run) {
                const std::size_t end = (std::min)(i + run, n);
                std::sort(v.begin() + static_cast<std::ptrdiff_t>(i),
                          v.begin() + static_cast<std::ptrdiff_t>(end));
            }
            break;
        }
        case P_EQUAL: std::fill(v.begin(), v.end(), 1.0); break;
        case P_FEW:
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i % 10);
            break;
        case P_NEAR_01:
        case P_NEAR_1:
        case P_NEAR_5: {
            std::sort(v.begin(), v.end());
            const double frac = (p == P_NEAR_01) ? 0.001
                              : (p == P_NEAR_1) ? 0.01 : 0.05;
            const std::size_t swaps =
                static_cast<std::size_t>(static_cast<double>(n) * frac);
            std::uniform_int_distribution<std::size_t> idx(0, n - 2);
            for (std::size_t k = 0; k < swaps; ++k) {
                const std::size_t i = idx(rng);
                std::swap(v[i], v[i + 1]);
            }
            break;
        }
        case P_PUSH_FRONT: {
            std::sort(v.begin(), v.end());
            const std::size_t m = n / 100;
            std::reverse(v.begin() + static_cast<std::ptrdiff_t>(n - m), v.end());
            std::rotate(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n - m),
                        v.end());
            break;
        }
        case P_PUSH_BACK: {
            std::sort(v.begin(), v.end());
            const std::size_t m = n / 100;
            std::rotate(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(m), v.end());
            break;
        }
    }
    return v;
}

void hybrid_core(double* a, std::size_t m, int depth, std::size_t MT,
                 oblivone::options& opt, std::vector<double>& buf) {
    if (m < 2) return;
    if (m <= kOriginalMax || depth >= opt.max_depth) {
        const auto key = [](const double& x) { return oblivone::key_of(x); };
        const auto less = [](const double& x, const double& y) {
            return less_d(x, y);
        };
        oblivone::default_fallback fb;
        oblivone::detail::sort_range(a, m, depth, key, less, false, fb, opt, buf);
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

    const std::uint64_t range = mx - mn;
    const double inv_range = 1.0 / static_cast<double>(range);
    std::vector<std::uint32_t> base(MT + 1, 0);
    std::vector<std::uint32_t> c(MT, 0);

    auto sub_from_u = [&](double u) -> std::size_t {
        std::size_t b = static_cast<std::size_t>(u * static_cast<double>(MT));
        return b >= MT ? MT - 1 : b;
    };
    for (std::size_t i = 0; i < m; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        ++c[sub_from_u(u)];
    }
    for (std::size_t b = 0; b < MT; ++b) base[b + 1] = base[b] + c[b];

    std::vector<std::uint32_t> off(m, 0);
    auto offset_from_u = [&](double u, std::size_t b) -> std::size_t {
        const double local = u * static_cast<double>(MT) - static_cast<double>(b);
        double t = local * static_cast<double>(c[b]);
        if (!(t > 0.0)) t = 0.0;
        std::size_t o = static_cast<std::size_t>(t);
        return o >= c[b] ? c[b] - 1 : o;
    };

    for (std::size_t i = 0; i < m; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        const std::size_t b = sub_from_u(u);
        ++off[base[b] + offset_from_u(u, b)];
    }

    for (std::size_t b = 0; b < MT; ++b) {
        std::uint32_t running = 0;
        for (std::size_t o = 0; o < c[b]; ++o) {
            const std::uint32_t cnt = off[base[b] + o];
            off[base[b] + o] = running;
            running += cnt;
        }
    }

    std::vector<double> local(m);
    for (std::size_t i = 0; i < m; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        const std::size_t b = sub_from_u(u);
        const std::size_t o = offset_from_u(u, b);
        const std::uint32_t pos = base[b] + off[base[b] + o]++;
        local[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(local.begin(), local.end(), a);

    for (std::size_t b = 0; b < MT; ++b) {
        for (std::size_t o = 0; o < c[b]; ++o) {
            const std::uint32_t end = base[b] + off[base[b] + o];
            const std::uint32_t begin =
                (o == 0) ? base[b] : base[b] + off[base[b] + o - 1];
            const std::uint32_t len = end - begin;
            if (len > 1) hybrid_core(a + begin, len, depth + 1, MT, opt, buf);
        }
    }
}

void hybrid_sort(double* a, std::size_t n, std::size_t MT) {
    oblivone::options opt;
    std::vector<double> buf(n);
    hybrid_core(a, n, 0, MT, opt, buf);
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

    std::printf("pattern,n,MT,base_ms,hybrid_ms,ratio\n");
    for (int p = P_RANDOM; p <= P_PUSH_BACK; ++p) {
        const auto data = make_pattern(p, n, 9000 + static_cast<std::uint64_t>(p));
        const auto base_sort = [&](std::vector<double>& v) {
            oblivone::oblivone_sort(v.data(), v.size(), key, opt);
        };
        const auto hybrid = [&](std::vector<double>& v) {
            hybrid_sort(v.data(), v.size(), MT);
        };

        {
            auto a = data; base_sort(a);
            auto b = data; hybrid(b);
            if (!std::is_sorted(a.begin(), a.end(), less_d) ||
                !std::is_sorted(b.begin(), b.end(), less_d)) {
                std::printf("%s MISMATCH\n", pattern_name(p));
                return 2;
            }
        }

        const double tb = median_ms(data, base_sort, reps);
        const double th = median_ms(data, hybrid, reps);
        std::printf("%s,%zu,%zu,%.3f,%.3f,%.3f\n", pattern_name(p), n, MT, tb, th,
                    th / tb);
    }
    return 0;
}

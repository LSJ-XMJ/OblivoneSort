// B sweep: configurable bucket cap, root group stats.
// Same core structure as Oblivone; no nested ordered scan (random-focused).
//
// Usage: bench_bscan [n] [reps]

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

struct stats {
    std::size_t root_groups = 0;
    std::size_t root_max = 0;
    std::size_t root_nested_elems = 0;
    std::size_t root_nested_calls = 0;
    double first = 0;
    double nested = 0;
    double total = 0;
};

struct opts {
    std::size_t Bcap = 65536;
    std::size_t small_sort = 16;
    std::size_t leaf_sort = 22;
    int max_depth = 16;
};

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

void core(double* a, std::size_t m, int depth, const opts& o,
          std::vector<double>& buf, stats* root) {
    if (m < 2) return;
    if (m < o.small_sort || depth >= o.max_depth) {
        std::sort(a, a + m, less_d);
        return;
    }

    const auto p0 = Clock::now();
    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < m; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(m, o.Bcap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double t = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(t);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++cnt[bucket_of(keyd(a[i]))];
    }
    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = bucket_of(keyd(a[i]));
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);
    if (root && depth == 0) root->first = std::chrono::duration<double, std::milli>(
                                                Clock::now() - p0).count();

    const auto pn = Clock::now();
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len < 2) continue;
        if (root && depth == 0) {
            ++root->root_groups;
            if (len > root->root_max) root->root_max = len;
        }
        if (len < o.leaf_sort) {
            if (len == 3) {
                sort3_key(a + begin);
            } else {
                insertion_less(a + begin, len);
            }
        } else {
            if (root && depth == 0) {
                ++root->root_nested_calls;
                root->root_nested_elems += len;
            }
            core(a + begin, len, depth + 1, o, buf, nullptr);
        }
    }
    if (root && depth == 0) {
        root->nested = std::chrono::duration<double, std::milli>(Clock::now() - pn).count();
        root->total = root->first + root->nested;
    }
}

void sort_with_b(double* a, std::size_t n, std::size_t Bcap, stats* s) {
    oblivone::options dummy;
    (void)dummy;
    opts o;
    o.Bcap = Bcap;
    std::vector<double> buf(n);
    if (s) *s = stats{};
    core(a, n, 0, o, buf, s);
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(7000 + static_cast<std::uint64_t>(p));
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

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const std::size_t Bs[] = {16384, 32768, 65536, 131072, 262144, 524288, 1048576};

    std::printf("pattern,n,B,first_ms,nested_ms,total_ms,root_groups,root_max,"
                "root_nested_elems,root_nested_calls\n");
    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);
        for (const std::size_t B : Bs) {
            auto v = data;
            stats s;
            sort_with_b(v.data(), v.size(), B, &s);
            if (!std::is_sorted(v.begin(), v.end(), less_d)) {
                std::printf("MISMATCH\n");
                return 2;
            }
            std::printf("%s,%zu,%zu,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu\n",
                        p == 0 ? "random" : (p == 1 ? "few" : "organ"), n, B,
                        s.first, s.nested, s.total, s.root_groups, s.root_max,
                        s.root_nested_elems, s.root_nested_calls);
        }
    }
    return 0;
}

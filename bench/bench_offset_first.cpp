// Prototype: only the FIRST level uses 4 sub-buckets + 4 offset tables
// (total n). Every offset group is then finished by the original Oblivone
// core (not std::sort).
//
// Usage: bench_offset_first [n] [reps] [MT]

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

struct timings {
    double minmax = 0;
    double sub_count = 0;
    double offset_alloc = 0;
    double offset_count = 0;
    double prefix = 0;
    double buf_alloc = 0;
    double scatter = 0;
    double copy = 0;
    double nested = 0;
    std::size_t groups = 0;
    std::size_t nested_calls = 0;
    std::size_t nested_elems = 0;
    std::size_t leaf_calls = 0;
};

struct core_timings {
    double minmax = 0;
    double count_alloc = 0;
    double prefix = 0;
    double buf_alloc = 0;
    double scatter = 0;
    double copy = 0;
    double nested = 0;
    std::size_t B = 0;
    std::size_t max_group = 0;
    std::size_t groups = 0;
    std::size_t nested_calls = 0;
    std::size_t nested_elems = 0;
    std::size_t leaf_calls = 0;
};

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

volatile double g_sink = 0.0;

double checksum(const double* a, std::size_t n) {
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += a[i];
    return s;
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(6000 + static_cast<std::uint64_t>(p));
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

// 只做 core 第一层：max/min、count、prefix、scatter、copy；不处理组。
void core_first_only(double* a, std::size_t n) {
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
    const std::size_t auto_cap =
        (n <= 2000000) ? (std::size_t(1) << 16) : (std::size_t(1) << 20);
    oblivone::options opt;
    const std::size_t cap = (std::min)(opt.max_buckets, auto_cap);
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(n, cap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double x = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(x);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ++cnt[bucket_of(keyd(a[i]))];
    }
    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }
    std::vector<double> buf(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t b = bucket_of(keyd(a[i]));
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.end(), a);
}

// 当前 core 的根层同口径计时（内部仍调用原 sort_range）。
void core_timed(double* a, std::size_t n, core_timings& t) {
    if (n < 2) return;
    auto p0 = Clock::now();
    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    t.minmax = ms_since(p0);
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    const std::size_t auto_cap =
        (n <= 2000000) ? (std::size_t(1) << 16) : (std::size_t(1) << 20);
    oblivone::options opt;
    const std::size_t cap = (std::min)(opt.max_buckets, auto_cap);
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(n, cap));
    t.B = B;
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double x = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(x);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    p0 = Clock::now();
    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ++cnt[bucket_of(keyd(a[i]))];
    }
    t.count_alloc = ms_since(p0);

    p0 = Clock::now();
    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }
    t.prefix = ms_since(p0);

    p0 = Clock::now();
    std::vector<double> buf(n);
    t.buf_alloc = ms_since(p0);

    p0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t b = bucket_of(keyd(a[i]));
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    t.scatter = ms_since(p0);

    p0 = Clock::now();
    std::copy(buf.begin(), buf.end(), a);
    t.copy = ms_since(p0);

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    const auto less = [](const double& x, const double& y) { return less_d(x, y); };
    oblivone::default_fallback fb;

    p0 = Clock::now();
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len < 2) continue;
        ++t.groups;
        if (len > t.max_group) t.max_group = len;
        if (len < opt.leaf_sort) {
            ++t.leaf_calls;
            if (len == 3) {
                sort3_key(a + begin);
            } else {
                insertion_less(a + begin, len);
            }
        } else if (len < opt.small_sort) {
            std::sort(a + begin, a + end, less);
        } else {
            ++t.nested_calls;
            t.nested_elems += len;
            oblivone::detail::sort_range(a + begin, len, 1, key, less, false, fb,
                                         opt, buf);
        }
    }
    t.nested = ms_since(p0);
}

void offset_first_sort(double* a, std::size_t n, std::size_t MT,
                       timings* tm = nullptr, bool do_nested = true) {
    if (n < 2) return;
    auto p0 = Clock::now();
    std::uint64_t mn = keyd(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t k = keyd(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (tm) tm->minmax = ms_since(p0);
    if (mn == mx) return;

    const std::uint64_t range = mx - mn;
    const double inv_range = 1.0 / static_cast<double>(range);
    auto sub_from_u = [&](double u) -> std::size_t {
        std::size_t b = static_cast<std::size_t>(u * static_cast<double>(MT));
        return b >= MT ? MT - 1 : b;
    };

    p0 = Clock::now();
    std::vector<std::uint32_t> base(MT + 1, 0);
    std::vector<std::uint32_t> c(MT, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        ++c[sub_from_u(u)];
    }
    for (std::size_t b = 0; b < MT; ++b) base[b + 1] = base[b] + c[b];
    if (tm) tm->sub_count = ms_since(p0);

    auto offset_from_u = [&](double u, std::size_t b) -> std::size_t {
        const double local = u * static_cast<double>(MT) - static_cast<double>(b);
        double t = local * static_cast<double>(c[b]);
        if (!(t > 0.0)) t = 0.0;
        std::size_t o = static_cast<std::size_t>(t);
        return o >= c[b] ? c[b] - 1 : o;
    };

    p0 = Clock::now();
    std::vector<std::uint32_t> off(n, 0);
    if (tm) tm->offset_alloc = ms_since(p0);

    p0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        const std::size_t b = sub_from_u(u);
        ++off[base[b] + offset_from_u(u, b)];
    }
    if (tm) tm->offset_count = ms_since(p0);

    p0 = Clock::now();
    for (std::size_t b = 0; b < MT; ++b) {
        std::uint32_t running = 0;
        for (std::size_t o = 0; o < c[b]; ++o) {
            const std::uint32_t cnt = off[base[b] + o];
            off[base[b] + o] = running;  // 相对子桶起点的下标
            running += cnt;
        }
    }
    if (tm) tm->prefix = ms_since(p0);

    p0 = Clock::now();
    std::vector<double> buf(n);
    if (tm) tm->buf_alloc = ms_since(p0);

    p0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
        const double u = static_cast<double>(keyd(a[i]) - mn) * inv_range;
        const std::size_t b = sub_from_u(u);
        const std::size_t o = offset_from_u(u, b);
        const std::uint32_t pos = base[b] + off[base[b] + o]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    if (tm) tm->scatter = ms_since(p0);

    p0 = Clock::now();
    std::copy(buf.begin(), buf.end(), a);
    if (tm) tm->copy = ms_since(p0);

    // 内部回到原 Oblivone 模板；depth=1，和父层递归进来一致。
    const auto key = [](const double& x) { return oblivone::key_of(x); };
    const auto less = [](const double& x, const double& y) { return less_d(x, y); };
    oblivone::options opt;
    oblivone::default_fallback fb;

    p0 = Clock::now();
    if (do_nested) {
        for (std::size_t b = 0; b < MT; ++b) {
            for (std::size_t o = 0; o < c[b]; ++o) {
                const std::uint32_t end = base[b] + off[base[b] + o];
                const std::uint32_t begin =
                    (o == 0) ? base[b] : base[b] + off[base[b] + o - 1];
                const std::uint32_t len = end - begin;
                if (len < 2) continue;
                if (tm) ++tm->groups;
                if (len < opt.leaf_sort) {
                    if (tm) ++tm->leaf_calls;
                    if (len == 3) {
                        sort3_key(a + begin);
                    } else {
                        insertion_less(a + begin, len);
                    }
                } else {
                    if (tm) {
                        ++tm->nested_calls;
                        tm->nested_elems += len;
                    }
                    oblivone::detail::sort_range(a + begin, len, 1, key, less,
                                                 false, fb, opt, buf);
                }
            }
        }
    }
    if (tm) tm->nested = ms_since(p0);
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
    const bool timing = (argc > 4) && std::atoi(argv[4]) != 0;

    const auto key = [](const double& x) { return oblivone::key_of(x); };
    oblivone::options opt;

    if (timing) {
        const auto data = make_pattern(0, n);
        timings t0;
        timings t1;
        auto a = data;
        offset_first_sort(a.data(), a.size(), MT, &t0, false);
        auto b = data;
        offset_first_sort(b.data(), b.size(), MT, &t1, true);

        core_timings tc;
        auto c = data;
        core_timed(c.data(), c.size(), tc);

        auto d = data;
        const auto pb0 = Clock::now();
        oblivone::oblivone_sort(d.data(), d.size(), key, opt);
        const double base_ms = ms_since(pb0);

        auto f = data;
        const auto pf0 = Clock::now();
        core_first_only(f.data(), f.size());
        const double core_first_ms = ms_since(pf0);

        auto e = data;
        {
            const auto kk = [](const double& x) { return oblivone::key_of(x); };
            const auto ll = [](const double& x, const double& y) {
                return less_d(x, y);
            };
            oblivone::default_fallback fbb;
            std::vector<double> bbuf(n);
            const auto pe0 = Clock::now();
            oblivone::detail::sort_range(e.data(), e.size(), 0, kk, ll, false,
                                         fbb, opt, bbuf);
            const double direct_ms = ms_since(pe0);
            std::printf("direct_sort_range,%.3f\n", direct_ms);
        }

        auto total = [](const timings& t) {
            return t.minmax + t.sub_count + t.offset_alloc + t.offset_count +
                   t.prefix + t.buf_alloc + t.scatter + t.copy + t.nested;
        };
        auto first = [](const timings& t) {
            return t.minmax + t.sub_count + t.offset_alloc + t.offset_count +
                   t.prefix + t.buf_alloc + t.scatter + t.copy;
        };
        auto core_total = [](const core_timings& t) {
            return t.minmax + t.count_alloc + t.prefix + t.buf_alloc + t.scatter +
                   t.copy + t.nested;
        };
        auto core_first = [](const core_timings& t) {
            return t.minmax + t.count_alloc + t.prefix + t.buf_alloc + t.scatter +
                   t.copy;
        };

        g_sink = checksum(a.data(), a.size()) + checksum(b.data(), b.size()) +
                 checksum(c.data(), c.size()) + checksum(d.data(), d.size()) +
                 checksum(e.data(), e.size()) + checksum(f.data(), f.size());

        std::printf("phase,core,offset_0,offset_nested\n");
        std::printf("base_oblivone_sort,%.3f\n", base_ms);
        std::printf("core_first_only,%.3f\n", core_first_ms);
        std::printf("core_first_plus_nested,%.3f\n", base_ms);
        std::printf("core_B,%zu\n", tc.B);
        std::printf("core_max_group,%zu\n", tc.max_group);
        std::printf("minmax,%.3f,%.3f,%.3f\n", tc.minmax, t0.minmax, t1.minmax);
        std::printf("count/sub_count,%.3f,%.3f,%.3f\n", tc.count_alloc,
                    t0.sub_count, t1.sub_count);
        std::printf("offset_alloc,0.000,%.3f,%.3f\n", t0.offset_alloc,
                    t1.offset_alloc);
        std::printf("offset_count,0.000,%.3f,%.3f\n", t0.offset_count,
                    t1.offset_count);
        std::printf("prefix,%.3f,%.3f,%.3f\n", tc.prefix, t0.prefix, t1.prefix);
        std::printf("buf_alloc,%.3f,%.3f,%.3f\n", tc.buf_alloc, t0.buf_alloc,
                    t1.buf_alloc);
        std::printf("scatter,%.3f,%.3f,%.3f\n", tc.scatter, t0.scatter, t1.scatter);
        std::printf("copy,%.3f,%.3f,%.3f\n", tc.copy, t0.copy, t1.copy);
        std::printf("nested,%.3f,%.3f,%.3f\n", tc.nested, t0.nested, t1.nested);
        std::printf("first_level,%.3f,%.3f,%.3f\n", core_first(tc), first(t0),
                    first(t1));
        std::printf("total,%.3f,%.3f,%.3f\n", core_total(tc), total(t0), total(t1));
        std::printf("groups,%zu,%zu,%zu\n", tc.groups, t0.groups, t1.groups);
        std::printf("nested_calls,%zu,%zu,%zu\n", tc.nested_calls, t0.nested_calls,
                    t1.nested_calls);
        std::printf("nested_elems,%zu,%zu,%zu\n", tc.nested_elems, t0.nested_elems,
                    t1.nested_elems);
        std::printf("leaf_calls,%zu,%zu,%zu\n", tc.leaf_calls, t0.leaf_calls,
                    t1.leaf_calls);
        if (!std::is_sorted(b.begin(), b.end(), less_d) ||
            !std::is_sorted(c.begin(), c.end(), less_d) ||
            !std::is_sorted(d.begin(), d.end(), less_d) ||
            !std::is_sorted(e.begin(), e.end(), less_d)) {
            std::printf("MISMATCH\n");
            return 2;
        }
        return 0;
    }

    std::printf("pattern,n,MT,base_ms,offset_first_ms,ratio\n");
    for (int p = 0; p < 3; ++p) {
        const auto data = make_pattern(p, n);
        const auto base_sort = [&](std::vector<double>& v) {
            oblivone::oblivone_sort(v.data(), v.size(), key, opt);
        };
        const auto off_sort = [&](std::vector<double>& v) {
            offset_first_sort(v.data(), v.size(), MT);
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

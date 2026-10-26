// Mean-centered bucket range:
//   mid = (min+max)/2
//   avg > mid -> new_min = 2*avg - max
//   avg < mid -> new_max = 2*avg - min
// Compare with the same core without centering.
//
// Usage: bench_mean_center [n] [reps]

#include <oblivone/sort.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t keyd(double x) { return oblivone::key_of(x); }
bool less_d(double a, double b) { return keyd(a) < keyd(b); }

struct opt {
    bool center = false;
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

void core(double* a, std::size_t m, int depth, const opt& o,
          std::vector<double>& buf) {
    if (m < 2) return;
    if (m < o.small_sort || depth >= o.max_depth) {
        std::sort(a, a + m, less_d);
        return;
    }

    double mn = a[0];
    double mx = a[0];
    std::uint64_t mnk = keyd(a[0]);
    std::uint64_t mxk = mnk;
    long double sum = 0.0L;
    if (o.center) sum = static_cast<long double>(a[0]);
    for (std::size_t i = 1; i < m; ++i) {
        const double x = a[i];
        if (x < mn) mn = x;
        if (x > mx) mx = x;
        const std::uint64_t k = keyd(x);
        if (k < mnk) mnk = k;
        if (k > mxk) mxk = k;
        if (o.center) sum += static_cast<long double>(x);
    }
    if (mn == mx) return;

    std::uint64_t lo = mnk;
    std::uint64_t hi = mxk;
    if (o.center) {
        const double avg = static_cast<double>(sum / static_cast<long double>(m));
        const double mid = (mn + mx) * 0.5;
        if (avg > mid) {
            const double nmin = 2.0 * avg - mx;
            if (nmin > mn && nmin < mx) lo = keyd(nmin);
        } else if (avg < mid) {
            const double nmax = 2.0 * avg - mn;
            if (nmax > mn && nmax < mx) hi = keyd(nmax);
        }
        if (lo >= hi) {
            lo = mnk;
            hi = mxk;
        }
    }

    const std::uint64_t range = hi - lo;
    if (range == 0) return;
    const std::size_t B = (std::max)(std::size_t(1), (std::min)(m, o.Bcap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        if (k <= lo) return 0;
        if (k >= hi) return B - 1;
        const double t = static_cast<double>(k - lo) * scale;
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

    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len < 2) continue;
        if (len < o.leaf_sort) {
            if (len == 3) {
                sort3_key(a + begin);
            } else {
                insertion_less(a + begin, len);
            }
        } else {
            core(a + begin, len, depth + 1, o, buf);
        }
    }
}

void run_sort(double* a, std::size_t n, bool center, std::size_t Bcap) {
    opt o;
    o.center = center;
    o.Bcap = Bcap;
    std::vector<double> buf(n);
    core(a, n, 0, o, buf);
}

std::vector<double> make_pattern(int p, std::size_t n) {
    std::mt19937_64 rng(9000 + static_cast<std::uint64_t>(p));
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::exponential_distribution<double> ed(1.0);
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    std::vector<double> v(n);
    for (auto& x : v) {
        switch (p) {
            case 0: x = d(rng); break;
            case 1: x = nd(rng); break;
            case 2: x = ed(rng); break;
            case 3: x = 1.0 / std::pow((std::max)(ud(rng), 1e-12), 1.0 / 1.5); break;
            default: x = static_cast<double>(rng() % 10); break;
        }
    }
    return v;
}

template <class T>
T median(std::vector<T> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void small_bench() {
    const std::size_t groups = 10000;
    const int reps = 5;
    const std::size_t ns[] = {2, 3, 4, 8, 16, 24, 32, 48, 64, 96, 128, 192, 256};

    std::printf("n,std_ns,insert_ns,base_ns,center_ns\n");
    std::mt19937_64 rng(12345);

    for (const std::size_t n : ns) {
        std::vector<double> data(groups * n);
        std::uniform_real_distribution<double> d(-1.0, 1.0);
        for (auto& x : data) x = d(rng);

        opt bo;
        bo.center = false;
        bo.Bcap = n;
        bo.small_sort = 0;
        bo.leaf_sort = 0;
        bo.max_depth = 64;
        opt co = bo;
        co.center = true;

        std::vector<double> buf(n);
        auto base_run = [&](double* p) { core(p, n, 0, bo, buf); };
        auto center_run = [&](double* p) { core(p, n, 0, co, buf); };
        auto std_run = [&](double* p) { std::sort(p, p + n, less_d); };
        auto insert_run = [&](double* p) { insertion_less(p, n); };

        auto check = [&](auto f) {
            auto w = data;
            for (std::size_t off = 0; off < w.size(); off += n) {
                f(w.data() + off);
            }
            for (std::size_t off = 0; off < w.size(); off += n) {
                if (!std::is_sorted(w.begin() + static_cast<std::ptrdiff_t>(off),
                                    w.begin() + static_cast<std::ptrdiff_t>(off + n),
                                    less_d)) {
                    std::printf("MISMATCH n=%zu\n", n);
                    std::exit(2);
                }
            }
        };
        check(std_run);
        check(insert_run);
        check(base_run);
        check(center_run);

        auto measure = [&](auto f) {
            std::vector<double> times;
            for (int r = 0; r < reps; ++r) {
                auto w = data;  // copy outside timer
                const auto t0 = Clock::now();
                for (std::size_t off = 0; off < w.size(); off += n) {
                    f(w.data() + off);
                }
                const auto t1 = Clock::now();
                times.push_back(
                    std::chrono::duration<double, std::nano>(t1 - t0).count() /
                    static_cast<double>(groups));
            }
            return median(times);
        };

        std::printf("%zu,%.2f,%.2f,%.2f,%.2f\n", n, measure(std_run),
                    measure(insert_run), measure(base_run), measure(center_run));
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n =
        (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                   : 1000000;
    const int reps = (argc > 2) ? std::atoi(argv[2]) : 3;
    const std::size_t Bcap =
        (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                   : 65536;

    if (argc > 4 && std::atoi(argv[4]) != 0) {
        small_bench();
        return 0;
    }

    std::printf("pattern,n,B,avg,mid,base_ms,center_ms,ratio\n");
    const char* names[] = {"random", "normal", "exponential", "pareto", "few"};
    for (int p = 0; p < 5; ++p) {
        const auto data = make_pattern(p, n);
        const auto base_run = [&](std::vector<double>& v) {
            run_sort(v.data(), v.size(), false, Bcap);
        };
        const auto center_run = [&](std::vector<double>& v) {
            run_sort(v.data(), v.size(), true, Bcap);
        };

        auto a = data; base_run(a);
        auto b = data; center_run(b);
        if (!std::is_sorted(a.begin(), a.end(), less_d) ||
            !std::is_sorted(b.begin(), b.end(), less_d)) {
            std::printf("MISMATCH\n");
            return 2;
        }

        double mn = data[0];
        double mx = data[0];
        long double sum = 0.0L;
        for (const double x : data) {
            if (x < mn) mn = x;
            if (x > mx) mx = x;
            sum += static_cast<long double>(x);
        }
        const double avg = static_cast<double>(sum / static_cast<long double>(n));
        const double mid = (mn + mx) * 0.5;

        std::vector<std::vector<double>> samples(2);
        std::vector<std::size_t> order = {0, 1};
        std::mt19937_64 rng(777 + static_cast<std::uint64_t>(p));
        for (int r = 0; r < (std::max)(1, reps); ++r) {
            std::shuffle(order.begin(), order.end(), rng);
            for (const std::size_t which : order) {
                auto v = data;
                const auto t0 = Clock::now();
                if (which == 0) {
                    base_run(v);
                } else {
                    center_run(v);
                }
                const auto t1 = Clock::now();
                samples[which].push_back(
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }
        const double tb = median(samples[0]);
        const double tc = median(samples[1]);
        std::printf("%s,%zu,%zu,%.6f,%.6f,%.3f,%.3f,%.3f\n", names[p], n, Bcap,
                    avg, mid, tb, tc, tc / tb);
    }
    return 0;
}

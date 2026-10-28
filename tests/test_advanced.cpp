#include <oblivone/advanced/engine.hpp>
#include <oblivone/advanced/level.hpp>
#include <oblivone/advanced/sort.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
int failures = 0;

void check_phase() {
    oblivone::advanced::engine engine(4);
    engine.start();
    std::atomic<int> counter{0};
    for (int i = 0; i < 2000; ++i) {
        engine.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); },
                      0, i % 4);
    }
    engine.wait();
    if (counter.load() != 2000) {
        std::printf("FAIL phase counter=%d\n", counter.load());
        ++failures;
    }

    std::atomic<int> leaves{0};
    std::function<void(int)> grow;
    grow = [&](int depth) {
        if (depth == 0) {
            leaves.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        engine.submit([&grow, depth] { grow(depth - 1); }, 1);
        engine.submit([&grow, depth] { grow(depth - 1); }, 1);
    };
    engine.submit([&] { grow(6); }, 0);
    engine.wait();
    if (leaves.load() != 64) {
        std::printf("FAIL tree leaves=%d\n", leaves.load());
        ++failures;
    }
}

void check_reservation() {
    oblivone::advanced::engine engine(4);
    engine.reserve(3);
    engine.start();
    std::atomic<int> done{0};
    for (int i = 0; i < 200; ++i) {
        engine.submit([&] {
            std::this_thread::yield();
            done.fetch_add(1, std::memory_order_relaxed);
        }, 2);
    }
    engine.wait();
    if (done.load() != 200) {
        std::printf("FAIL reservation done=%d\n", done.load());
        ++failures;
    }
}

void check_exception() {
    oblivone::advanced::engine engine(2);
    engine.start();
    engine.submit([] { throw std::runtime_error("engine failure"); }, 0);
    bool caught = false;
    try {
        engine.wait();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    if (!caught) {
        std::printf("FAIL engine exception\n");
        ++failures;
    }
}

void check_sort() {
    std::mt19937_64 rng(6161);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    std::vector<double> values(300000);
    for (auto& x : values) x = d(rng);

    auto reference = values;
    std::sort(reference.begin(), reference.end(),
              [](double a, double b) {
                  return oblivone::detail::key_of(a) <
                         oblivone::detail::key_of(b);
              });

    oblivone::advanced::options popt;
    popt.threads = 4;
    popt.min_n = 1000;
    oblivone::advanced::parallel_sort(values, oblivone::options{}, popt);

    for (std::size_t i = 1; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) <
            oblivone::detail::key_of(values[i - 1])) {
            std::printf("FAIL advanced order at %zu\n", i);
            ++failures;
            return;
        }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) !=
            oblivone::detail::key_of(reference[i])) {
            std::printf("FAIL advanced multiset at %zu\n", i);
            ++failures;
            return;
        }
    }

    std::vector<double> special{
        2.0, (std::numeric_limits<double>::quiet_NaN)(), -0.0, 0.0,
        -(std::numeric_limits<double>::infinity)(),
        (std::numeric_limits<double>::infinity)()};
    oblivone::advanced::parallel_sort(special, oblivone::options{}, popt);
    for (double x : special) {
        (void)x;
    }

    std::vector<double> organ(300000);
    for (std::size_t i = 0; i < organ.size(); ++i) {
        organ[i] = static_cast<double>(
            i < organ.size() / 2 ? i : organ.size() - i);
    }
    auto organ_ref = organ;
    std::sort(organ_ref.begin(), organ_ref.end());
    oblivone::advanced::parallel_sort(organ, oblivone::options{}, popt);
    for (std::size_t i = 0; i < organ.size(); ++i) {
        if (oblivone::detail::key_of(organ[i]) !=
            oblivone::detail::key_of(organ_ref[i])) {
            std::printf("FAIL advanced organ at %zu\n", i);
            ++failures;
            return;
        }
    }
}

void check_executor_and_budget() {
    oblivone::advanced::executor ex(4);
    std::mt19937_64 rng(8181);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    oblivone::advanced::options popt;
    popt.min_n = 1000;

    for (int round = 0; round < 3; ++round) {
        std::vector<double> values(100000);
        for (auto& x : values) x = d(rng);
        oblivone::advanced::parallel_sort(ex, values, oblivone::options{},
                                          popt);
        if (!std::is_sorted(values.begin(), values.end(),
                            [](double a, double b) {
                                return oblivone::detail::less_double(a, b);
                            })) {
            std::printf("FAIL advanced executor round %d\n", round);
            ++failures;
            return;
        }
    }

    std::vector<double> values(100000);
    for (auto& x : values) x = d(rng);
    auto reference = values;
    std::sort(reference.begin(), reference.end());
    oblivone::advanced::options tight;
    tight.threads = 2;
    tight.min_n = 1000;
    tight.memory_budget = 1;
    oblivone::advanced::parallel_sort(values, oblivone::options{}, tight);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (oblivone::detail::key_of(values[i]) !=
            oblivone::detail::key_of(reference[i])) {
            std::printf("FAIL advanced memory fallback at %zu\n", i);
            ++failures;
            return;
        }
    }
}

void check_allocator() {
    std::mt19937_64 rng(7171);
    for (int trial = 0; trial < 20000; ++trial) {
        const std::size_t count = 1 + rng() % 50;
        std::vector<std::size_t> lengths(count);
        std::size_t total = 0;
        for (auto& len : lengths) {
            len = 1 + rng() % 256;
            total += len;
        }
        std::vector<oblivone::advanced::span> groups;
        std::size_t pos = 0;
        for (std::size_t len : lengths) {
            groups.push_back({pos, pos + len});
            pos += len;
        }
        oblivone::advanced::allocator alloc(
            groups, total, 1 + rng() % 6, 2 + rng() % 12, 0);

        std::vector<int> mark(total, 0);
        for (;;) {
            if ((rng() % 4) == 0) {
                alloc.donate(1 + rng() % 3, 1 + rng() % 2);
            }
            oblivone::advanced::plan plan;
            if (!alloc.next(plan)) break;
            for (const auto& s : plan.spans) {
                if (s.first > s.last || s.last > total) {
                    std::printf("FAIL allocator span\n");
                    ++failures;
                    return;
                }
                for (std::size_t i = s.first; i < s.last; ++i) ++mark[i];
            }
        }
        for (std::size_t i = 0; i < total; ++i) {
            if (mark[i] != 1) {
                std::printf("FAIL allocator cover trial=%d i=%zu mark=%d\n",
                            trial, i, mark[i]);
                ++failures;
                return;
            }
        }
    }
}
}  // namespace

int main() {
    check_phase();
    check_reservation();
    check_exception();
    check_allocator();
    check_sort();
    check_executor_and_budget();
    if (failures == 0) {
        std::printf("test_advanced passed\n");
        return 0;
    }
    return 1;
}

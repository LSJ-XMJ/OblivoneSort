// Copyright 2026 肖明杰 (Xiao Mingjie)
// SPDX-License-Identifier: Apache-2.0
//
// OblivoneSort — a distribution sort that computes each element's final
// position directly, instead of comparing elements.
//
// The public sort() / sort_by() entry points are the XSort dispatcher: they
// inspect the input (size, already ordered) and either fall back to a
// comparison sort or run the OblivoneSort core.

#ifndef OBLIVONE_SORT_HPP
#define OBLIVONE_SORT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace oblivone {

struct options {
    std::size_t small_sort  = 64;                    // <= this: comparison sort
    int         max_depth   = 16;                    // deeper: comparison sort
    std::size_t max_buckets = std::size_t(1) << 20;  // cap on bucket table
};

// Default fallback used by the XSort dispatcher. Users may inject their own
// (it is called on small ranges and when the recursion depth is exhausted).
struct default_fallback {
    template <class T, class Cmp>
    void operator()(T* first, T* last, Cmp comp) const {
        std::sort(first, last, comp);
    }
};

namespace detail {

// Order-preserving maps from keys to uint64_t.
inline std::uint64_t to_key(std::int64_t v) noexcept {
    return static_cast<std::uint64_t>(v) ^ 0x8000000000000000ull;
}
inline std::uint64_t to_key(std::uint32_t v) noexcept { return v; }
inline std::uint64_t to_key(std::int32_t v) noexcept {
    return to_key(static_cast<std::int64_t>(v));
}
inline std::uint64_t to_key(std::uint64_t v) noexcept { return v; }

inline std::uint64_t to_key(double v) noexcept {
    if (v != v) return std::numeric_limits<std::uint64_t>::max();  // NaN sorts last
    if (v == 0.0) v = 0.0;                                        // -0.0 == +0.0
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    return (u & 0x8000000000000000ull) ? ~u : (u | 0x8000000000000000ull);
}
inline std::uint64_t to_key(float v) noexcept {
    return to_key(static_cast<double>(v));
}

// floor((k - mn) * B / range). The implementation below uses a precomputed
// double scale for speed; double conversion, multiplication by a positive
// constant and truncation are all monotone, so bucket order is preserved.
// An exact 128-bit variant is listed on the roadmap.

}  // namespace detail

// Public, order-preserving key map (uint64_t). Users of sort_by() can use
// this for arithmetic keys, or provide their own monotone mapping.
template <class T>
std::uint64_t key_of(const T& v) noexcept {
    return detail::to_key(v);
}

template <class T, class KeyFn>
inline void insertion_sort(T* a, std::size_t n, const KeyFn& key) {
    for (std::size_t i = 1; i < n; ++i) {
        T v = a[i];
        const std::uint64_t kv = key(v);
        std::size_t j = i;
        while (j > 0 && kv < key(a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

// Sort one range. `buf` is scratch of at least `m` elements; it is reused
// across the recursion, so no per-level allocation happens.
template <class T, class KeyFn, class Fallback>
void sort_range(T* a, std::size_t m, int depth, const KeyFn& key,
                const Fallback& fallback, const options& opt, std::vector<T>& buf) {
    if (m < 2) return;

    if (m <= opt.small_sort || depth >= opt.max_depth) {
        fallback(a, a + m, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }

    std::uint64_t mn = key(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < m; ++i) {
        const std::uint64_t k = key(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;  // all keys equal

    const std::uint64_t range = mx - mn;
    const std::size_t B = (std::min)(m, opt.max_buckets);
    const double scale = static_cast<double>(B) / static_cast<double>(range);

    // Monotone by construction: conversion to double, multiplication by a
    // positive constant and truncation are all non-decreasing.
    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double t = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(t);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++cnt[bucket_of(key(a[i]))];
    }

    std::vector<std::uint64_t> start(B);
    std::uint64_t s = 0;
    for (std::size_t b = 0; b < B; ++b) {
        start[b] = s;
        s += cnt[b];
    }

    // Fill each block from its end backwards.
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = bucket_of(key(a[i]));
        const std::uint64_t pos = start[b] + cnt[b] - 1;
        --cnt[b];
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);

    for (std::size_t b = 0; b < B; ++b) {
        const std::uint64_t end = (b + 1 < B) ? start[b + 1] : s;
        const std::uint64_t len = end - start[b];
        if (len <= 1) continue;
        if (len <= 16) {
            insertion_sort(a + start[b], static_cast<std::size_t>(len), key);
        } else {
            sort_range(a + start[b], static_cast<std::size_t>(len), depth + 1,
                       key, fallback, opt, buf);
        }
    }
}

// XSort dispatcher: decides whether to fall back to a comparison sort or run
// the OblivoneSort core. `fallback` is called on small ranges and when the
// recursion depth is exhausted; it defaults to std::sort.
template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(T* data, std::size_t n, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{}) {
    if (n < 2) return;
    if (n > 0xFFFFFFFFull) {  // count table is 32-bit
        fallback(data, data + n, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }

    // Fast exit for already ordered input; stops after a few elements on
    // random data, so it costs almost nothing.
    bool ascending = true;
    bool descending = true;
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint64_t a = key(data[i - 1]);
        const std::uint64_t b = key(data[i]);
        if (a > b) ascending = false;
        if (a < b) descending = false;
        if (!ascending && !descending) break;
    }
    if (ascending) return;
    if (descending) {
        std::reverse(data, data + n);
        return;
    }

    std::vector<T> buf(n);
    sort_range(data, n, 0, key, fallback, opt, buf);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(std::vector<T>& v, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{}) {
    sort_by(v.data(), v.size(), key, opt, fallback);
}

// OblivoneSort core: no size/order dispatch; used by tests and advanced users.
template <class T, class KeyFn, class Fallback = default_fallback>
void oblivone_sort(T* data, std::size_t n, KeyFn key,
                   const options& opt = {}, Fallback fallback = Fallback{}) {
    if (n < 2) return;
    if (n > 0xFFFFFFFFull) {
        fallback(data, data + n, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }
    std::vector<T> buf(n);
    sort_range(data, n, 0, key, fallback, opt, buf);
}

// Convenience for arithmetic key types.
template <class T, class Fallback = default_fallback>
void sort(T* data, std::size_t n,
          const options& opt = {}, Fallback fallback = Fallback{}) {
    sort_by(data, n, [](const T& v) { return detail::to_key(v); }, opt, fallback);
}

template <class T, class Fallback = default_fallback>
void sort(std::vector<T>& v,
          const options& opt = {}, Fallback fallback = Fallback{}) {
    sort(v.data(), v.size(), opt, fallback);
}

}  // namespace oblivone

#endif  // OBLIVONE_SORT_HPP

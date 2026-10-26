#ifndef OBLIVONE_DETAIL_CORE_HPP
#define OBLIVONE_DETAIL_CORE_HPP

#include "../options.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace oblivone {
namespace detail {

template <class T, class Less>
void sort2_less(T* a, const Less& less) {
    if (less(a[1], a[0])) std::swap(a[0], a[1]);
}

template <class T, class Less>
void sort3_less(T* a, const Less& less) {
    if (less(a[1], a[0])) std::swap(a[0], a[1]);
    if (less(a[2], a[1])) std::swap(a[1], a[2]);
    if (less(a[1], a[0])) std::swap(a[0], a[1]);
}

// 带预算的插入排序。
// true 表示预算内完成；false 表示预算耗尽，但元素已经全部放回数组。
template <class T, class Less>
bool budgeted_insertion(T* first, std::size_t n, const Less& less,
                        std::size_t budget) {
    std::size_t moves = 0;
    for (std::size_t i = 1; i < n; ++i) {
        T value = std::move(first[i]);
        std::size_t j = i;
        while (j > 0 && less(value, first[j - 1])) {
            if (moves >= budget) {
                first[j] = std::move(value);
                return false;
            }
            first[j] = std::move(first[j - 1]);
            ++moves;
            --j;
        }
        first[j] = std::move(value);
    }
    return true;
}

// 对 [a, a+m) 做一次分桶排序。
// buf 至少 m 个元素；递归层共用同一个 buf。
template <class T, class KeyFn, class LessFn, class Fallback>
void sort_range(T* a, std::size_t m, int depth, const KeyFn& key,
                const LessFn& less, Fallback& fallback, const options& opt,
                std::vector<T>& buf) {
    if (m < 2) return;

    // 嵌套块有序快速路径；顶层已经由 dispatcher 做过。
    if (depth > 0 && opt.scan_threshold > 0 && m > opt.scan_threshold) {
        bool ascending = true;
        bool descending = true;
        for (std::size_t i = 1; i < m; ++i) {
            if (less(a[i], a[i - 1])) ascending = false;
            if (less(a[i - 1], a[i])) descending = false;
            if (!ascending && !descending) break;
        }
        if (ascending) return;
        if (descending) {
            std::reverse(a, a + m);
            return;
        }
        if (budgeted_insertion(a, m, less, m / 8)) return;
    }

    if (m < opt.small_sort || depth >= opt.max_depth) {
        fallback(a, a + m, less);
        return;
    }

    std::uint64_t minimum = key(a[0]);
    std::uint64_t maximum = minimum;
    for (std::size_t i = 1; i < m; ++i) {
        const std::uint64_t k = key(a[i]);
        if (k < minimum) minimum = k;
        if (k > maximum) maximum = k;
    }
    if (minimum == maximum) return;

    const std::size_t limit = opt.bucket_limit ? opt.bucket_limit : 1;
    const std::size_t buckets =
        (std::max)(std::size_t(1), (std::min)(m, limit));
    const std::uint64_t range = maximum - minimum;
    const double scale =
        static_cast<double>(buckets) / static_cast<double>(range);

    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        if (k <= minimum) return 0;
        if (k >= maximum) return buckets - 1;
        const double pos = static_cast<double>(k - minimum) * scale;
        const std::size_t b = static_cast<std::size_t>(pos);
        return b >= buckets ? buckets - 1 : b;
    };

    std::vector<std::uint32_t> count(buckets, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++count[bucket_of(key(a[i]))];
    }

    std::vector<std::uint32_t> start(buckets, 0);
    std::vector<std::uint32_t> end(buckets, 0);
    std::uint32_t sum = 0;
    for (std::size_t b = 0; b < buckets; ++b) {
        start[b] = sum;
        sum += count[b];
        end[b] = sum;
    }

    std::vector<std::uint32_t> pos = start;
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = bucket_of(key(a[i]));
        buf[static_cast<std::size_t>(pos[b]++)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);

    for (std::size_t b = 0; b < buckets; ++b) {
        const std::size_t len =
            static_cast<std::size_t>(end[b] - start[b]);
        if (len < 2) continue;

        T* group = a + start[b];
        if (len < opt.leaf_sort) {
            if (len == 2) {
                sort2_less(group, less);
            } else if (len == 3) {
                sort3_less(group, less);
            } else {
                for (std::size_t i = 1; i < len; ++i) {
                    T value = std::move(group[i]);
                    std::size_t j = i;
                    while (j > 0 && less(value, group[j - 1])) {
                        group[j] = std::move(group[j - 1]);
                        --j;
                    }
                    group[j] = std::move(value);
                }
            }
        } else if (len < opt.small_sort) {
            fallback(group, group + len, less);
        } else {
            sort_range(group, len, depth + 1, key, less, fallback, opt, buf);
        }
    }
}

}  // namespace detail
}  // namespace oblivone

#endif  // OBLIVONE_DETAIL_CORE_HPP

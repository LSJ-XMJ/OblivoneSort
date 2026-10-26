#ifndef OBLIVONE_DETAIL_DISPATCH_HPP
#define OBLIVONE_DETAIL_DISPATCH_HPP

#include "core.hpp"
#include "../options.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace oblivone {
namespace detail {

// 返回 true 表示还需要进入核心分桶；false 表示已经有序或已交给 fallback。
template <class T, class KeyFn, class LessFn, class Fallback>
bool prepare(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
             const options& opt, Fallback& fallback) {
    (void)key;
    if (n < 2) return false;

    if (n > 0xFFFFFFFFull) {
        fallback(data, data + n, less);
        return false;
    }

    if (n <= opt.small_top) {
        fallback(data, data + n, less);
        return false;
    }

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = 1; i < n; ++i) {
        if (less(data[i], data[i - 1])) ascending = false;
        if (less(data[i - 1], data[i])) descending = false;
        if (!ascending && !descending) break;
    }
    if (ascending) return false;
    if (descending) {
        std::reverse(data, data + n);
        return false;
    }

    if (budgeted_insertion(data, n, less, n / 8)) return false;
    return true;
}

template <class T, class KeyFn, class LessFn, class Fallback>
void sort_impl(T* data, std::size_t n, const KeyFn& key, const LessFn& less,
               const options& opt, Fallback fallback) {
    if (!prepare(data, n, key, less, opt, fallback)) return;

    std::vector<T> buf(n);
    sort_range(data, n, 0, key, less, fallback, opt, buf);
}

}  // namespace detail
}  // namespace oblivone

#endif  // OBLIVONE_DETAIL_DISPATCH_HPP

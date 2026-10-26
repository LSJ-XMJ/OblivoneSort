#ifndef OBLIVONE_DETAIL_FALLBACK_HPP
#define OBLIVONE_DETAIL_FALLBACK_HPP

#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace oblivone {
namespace detail {

template <class T, class Less>
void insertion_sort_less(T* first, std::size_t n, const Less& less) {
    for (std::size_t i = 1; i < n; ++i) {
        T value = first[i];
        std::size_t j = i;
        while (j > 0 && less(value, first[j - 1])) {
            first[j] = first[j - 1];
            --j;
        }
        first[j] = value;
    }
}

}  // namespace detail

// 默认 fallback：
//   * 算术类型的小段用插入排序；
//   * 其他情况用 std::sort；
//   * 始终使用调用者传入的比较器。
struct default_fallback {
    template <class T, class Less>
    void operator()(T* first, T* last, Less less) const {
        const std::size_t n = static_cast<std::size_t>(last - first);
        if (n < 2) return;

        if constexpr (std::is_arithmetic<T>::value) {
            if (n <= 96) {
                detail::insertion_sort_less(first, n, less);
                return;
            }
        }
        std::sort(first, last, less);
    }
};

}  // namespace oblivone

#endif  // OBLIVONE_DETAIL_FALLBACK_HPP

#ifndef OBLIVONE_SORT_HPP
#define OBLIVONE_SORT_HPP

#include "detail/dispatch.hpp"
#include "detail/fallback.hpp"
#include "detail/key.hpp"
#include "options.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace oblivone {

// 公共 key 映射；自定义算术类型或需要显式 key 时使用。
template <class T>
std::uint64_t key_of(const T& value) {
    return detail::key_of(value);
}

template <class T, class Fallback = default_fallback>
void sort(T* data, std::size_t n, const options& opt = {},
          Fallback fallback = Fallback{}) {
    auto key = [](const T& value) { return detail::key_of(value); };

    if constexpr (std::is_same<T, double>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_double(a, b);
        };
        detail::sort_impl(data, n, key, less, opt, fallback);
    } else if constexpr (std::is_same<T, float>::value) {
        auto less = [](const T& a, const T& b) {
            return detail::less_float(a, b);
        };
        detail::sort_impl(data, n, key, less, opt, fallback);
    } else {
        auto less = [&key](const T& a, const T& b) {
            return key(a) < key(b);
        };
        detail::sort_impl(data, n, key, less, opt, fallback);
    }
}

template <class T, class Fallback = default_fallback>
void sort(std::vector<T>& values, const options& opt = {},
          Fallback fallback = Fallback{}) {
    sort(values.data(), values.size(), opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(T* data, std::size_t n, KeyFn user_key, const options& opt = {},
             Fallback fallback = Fallback{}) {
    auto key = [&user_key](const T& value) -> std::uint64_t {
        return static_cast<std::uint64_t>(user_key(value));
    };
    auto less = [&key](const T& a, const T& b) {
        return key(a) < key(b);
    };
    detail::sort_impl(data, n, key, less, opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(std::vector<T>& values, KeyFn user_key, const options& opt = {},
             Fallback fallback = Fallback{}) {
    sort_by(values.data(), values.size(), user_key, opt, fallback);
}

// 项目专有别名。
template <class T, class Fallback = default_fallback>
void xsort(T* data, std::size_t n, const options& opt = {},
           Fallback fallback = Fallback{}) {
    sort(data, n, opt, fallback);
}

template <class T, class Fallback = default_fallback>
void xsort(std::vector<T>& values, const options& opt = {},
           Fallback fallback = Fallback{}) {
    sort(values, opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void xsort_by(T* data, std::size_t n, KeyFn key, const options& opt = {},
              Fallback fallback = Fallback{}) {
    sort_by(data, n, key, opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void xsort_by(std::vector<T>& values, KeyFn key, const options& opt = {},
              Fallback fallback = Fallback{}) {
    sort_by(values, key, opt, fallback);
}

}  // namespace oblivone

#endif  // OBLIVONE_SORT_HPP

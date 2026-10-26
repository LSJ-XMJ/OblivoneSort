#ifndef OBLIVONE_DETAIL_KEY_HPP
#define OBLIVONE_DETAIL_KEY_HPP

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace oblivone {
namespace detail {

// 把算术类型映射成保序的 uint64_t。
//
// 约定：
//   * 非 NaN 值严格保序；
//   * 所有 NaN 映射到 UINT64_MAX；
//   * -0.0 与 +0.0 映射到同一个 key；
//   * 不承诺 NaN 之间的顺序。
template <class T>
std::uint64_t key_of(const T& value) {
    if constexpr (std::is_same<T, bool>::value) {
        return value ? 1u : 0u;
    } else if constexpr (std::is_integral<T>::value) {
        if constexpr (std::is_signed<T>::value) {
            // 先符号扩展到 64 位，再翻转符号位。
            const std::uint64_t u =
                static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
            return u ^ (std::uint64_t(1) << 63);
        } else {
            return static_cast<std::uint64_t>(value);
        }
    } else if constexpr (std::is_floating_point<T>::value) {
        static_assert(std::is_same<T, float>::value ||
                          std::is_same<T, double>::value,
                      "key_of 只支持 float/double；其他浮点类型请用 sort_by");
        static_assert(std::numeric_limits<T>::is_iec559,
                      "浮点 key 需要 IEEE-754 表示");

        if (std::isnan(value)) return (std::numeric_limits<std::uint64_t>::max)();

        T v = value;
        if (v == static_cast<T>(0)) v = static_cast<T>(0);  // -0 -> +0

        if constexpr (std::is_same<T, float>::value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            const std::uint32_t sign = std::uint32_t(1) << 31;
            const std::uint32_t mapped =
                (bits & sign) ? static_cast<std::uint32_t>(~bits)
                              : static_cast<std::uint32_t>(bits | sign);
            return static_cast<std::uint64_t>(mapped);
        } else {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            const std::uint64_t sign = std::uint64_t(1) << 63;
            return (bits & sign) ? static_cast<std::uint64_t>(~bits)
                                 : static_cast<std::uint64_t>(bits | sign);
        }
    } else {
        static_assert(std::is_arithmetic<T>::value,
                      "默认 sort 只支持算术类型；请使用 sort_by");
        return 0;
    }
}

inline bool less_double(double a, double b) {
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    return a < b;
}

inline bool less_float(float a, float b) {
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    return a < b;
}

}  // namespace detail
}  // namespace oblivone

#endif  // OBLIVONE_DETAIL_KEY_HPP

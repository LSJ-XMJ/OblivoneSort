#ifndef OBLIVONE_DETAIL_COARSE_HPP
#define OBLIVONE_DETAIL_COARSE_HPP

#include <algorithm>
#include <cstddef>

namespace oblivone {
namespace detail {

// Root coarse partition for numeric values. The public key mapping is
// order-preserving but not value-linear, so value ranges are used here only
// when the input is arithmetic and contains no NaN.
inline std::size_t automatic_coarse_buckets(std::size_t n) {
    return (std::max)(std::size_t(512),
                      (std::min)(std::size_t(1024), n / 8000));
}

inline std::size_t automatic_local_buckets(std::size_t n) {
    if (n <= 3000000) return 1024;
    if (n <= 15000000) return 2048;
    return 4096;
}

struct value_bucket_map {
    bool enabled = false;
    double minimum = 0.0;
    double scale = 1.0;
    std::size_t buckets = 1;

    std::size_t index(double value) const {
        const double x = (value - minimum) * scale;
        if (x <= 0.0) return 0;
        if (x >= static_cast<double>(buckets)) return buckets - 1;
        return static_cast<std::size_t>(x);
    }
};

inline value_bucket_map make_value_bucket_map(double minimum, double maximum,
                                              std::size_t buckets) {
    value_bucket_map map;
    if (!(maximum > minimum) || buckets < 2) return map;
    map.enabled = true;
    map.minimum = minimum;
    map.buckets = buckets;
    map.scale = static_cast<double>(buckets) / (maximum - minimum);
    return map;
}

}  // namespace detail
}  // namespace oblivone

#endif  // OBLIVONE_DETAIL_COARSE_HPP

// Copyright 2026 肖明杰 (Xiao Mingjie)
// SPDX-License-Identifier: Apache-2.0
//
// OblivoneSort：直接计算每个元素最终位置的分布排序。
//
// 流程（XSort 调度器 → OblivoneSort 核心）：
//   1. 把键映射成保序的 uint64_t；
//   2. 把当前区间按值域等分成 m 个桶；
//   3. 计数，前缀和得到每个桶的精确起点；
//   4. 把元素直接搬到对应块（置换，不比较）；
//   5. 块内排序：极小用插入排序，较大继续递归；
//   6. 段太小或递归过深时交给比较排序兜底。
//
// 不变量：
//   * 桶映射单调，所以每个桶是连续值区间；
//   * start[] 是前缀和，所以各块不重叠且覆盖 [0, m)；
//   * 块内排好、按块拼接，整体有序。
//
// 成本：每层 O(m)；局部密度比有界时期望 O(n)；有深度保护和兜底，
// 最坏 O(n log n)。额外内存 O(n)：一块复用缓冲 + 每层一张计数表。
// 不稳定。

#ifndef OBLIVONE_SORT_HPP
#define OBLIVONE_SORT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace oblivone {

// 参数。默认值见 README；改动时同步更新文档和基准。
struct options {
    // 调度层：n ≤ small_sort 交兜底；核心内：组 < leaf_sort 插入，≥ 继续。
    std::size_t small_sort = 16;

    // 叶子阈值。当前取 22：按 1:1:1:1 权重算出的中位最优值。
    // 如果以后能拿到真实分布概率，应重新标定这个值（见 docs/design.md §7）。
    // 0 表示不插入（纯 Oblivone，实验用）。
    std::size_t leaf_sort = 22;

    // 递归深度上限，防止自相似/对抗数据无限细分；到顶就交给兜底。
    int max_depth = 16;

    // 每层桶数上限。内部还会按 n 自适应：n ≤ 2e6 用 64K，更大用 1M；
    // 这里再设一个上界，0 按 1 处理。
    std::size_t max_buckets = std::size_t(1) << 20;
};

// 兜底排序的契约（XSort 的一部分）：
//   * 在段长 < small_sort、或递归到 max_depth 时被调用；
//   * 深度兜底时可能拿到很大的段，所以它必须是 O(k log k) 最坏；
//   * 必须使用传入的比较器；
//   * 可以抛异常，此时只提供基本异常保证。
struct default_fallback {
    template <class T, class Cmp>
    void operator()(T* first, T* last, Cmp comp) const {
        std::sort(first, last, comp);
    }
};

// 内部实现：接口不保证稳定；测试/基准会直接使用，改动时同步更新。
namespace detail {

// int64 补码 -> 无符号，保序：
//   INT64_MIN -> 0，-1 -> 0x7FFF...，0 -> 0x8000...，INT64_MAX -> UINT64_MAX。
inline std::uint64_t to_key(std::int64_t v) noexcept {
    return static_cast<std::uint64_t>(v) ^ 0x8000000000000000ull;
}
inline std::uint64_t to_key(std::uint32_t v) noexcept { return v; }
inline std::uint64_t to_key(std::int32_t v) noexcept {
    return to_key(static_cast<std::int64_t>(v));
}
inline std::uint64_t to_key(std::uint64_t v) noexcept { return v; }

// IEEE-754 -> 保序 uint64_t：
//   正数置符号位；负数按位取反。这样 -inf 映射到 0，-0.0 在低位。
//   NaN 映射为 UINT64_MAX，排在最后；-0.0 归一化成 +0.0，与浮点比较一致。
inline std::uint64_t to_key(double v) noexcept {
    if (v != v) return std::numeric_limits<std::uint64_t>::max();  // NaN 排最后
    if (v == 0.0) v = 0.0;                                        // -0.0 == +0.0
    std::uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    return (u & 0x8000000000000000ull) ? ~u : (u | 0x8000000000000000ull);
}
inline std::uint64_t to_key(float v) noexcept {
    return to_key(static_cast<double>(v));
}

// 极小块的插入排序：省掉递归调用和桶表的开销。
template <class T, class KeyFn>
inline void insertion_sort(T* a, std::size_t n, const KeyFn& key) {
    for (std::size_t i = 1; i < n; ++i) {
        T v = a[i];
        const std::uint64_t kv = key(v);  // 每个元素只算一次键
        std::size_t j = i;
        while (j > 0 && kv < key(a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

// 近有序快速路径：带预算的插入排序。
// 完成返回 true；超过 budget 次搬移则中止并返回 false。
// 中止时把当前元素放回空位，数组仍是输入的置换，可以继续交给 Oblivone。
template <class T, class KeyFn>
inline bool budgeted_insertion(T* a, std::size_t n, const KeyFn& key,
                               std::size_t budget) {
    std::size_t moves = 0;
    for (std::size_t i = 1; i < n; ++i) {
        T v = a[i];
        const std::uint64_t kv = key(v);
        std::size_t j = i;
        while (j > 0 && kv < key(a[j - 1])) {
            a[j] = a[j - 1];
            --j;
            if (++moves > budget) {
                a[j] = v;  // 放回空位，保持置换
                return false;
            }
        }
        a[j] = v;
    }
    return true;
}

// 排一个区间。buf 是长度至少 m 的复用缓冲，整棵递归树共用；
// 计数表目前每个递归节点各分配一次（复用列在路线图里）。
template <class T, class KeyFn, class Fallback>
void sort_range(T* a, std::size_t m, int depth, const KeyFn& key,
                const Fallback& fallback, const options& opt, std::vector<T>& buf) {
    if (m < 2) return;

    // 终止：段太小（严格小于 16），或深度到顶。都交给用户提供的比较排序。
    if (m < opt.small_sort || depth >= opt.max_depth) {
        fallback(a, a + m, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }

    // 1. 键范围：一遍扫描同时得到 min 和 max。
    std::uint64_t mn = key(a[0]);
    std::uint64_t mx = mn;
    for (std::size_t i = 1; i < m; ++i) {
        const std::uint64_t k = key(a[i]);
        if (k < mn) mn = k;
        if (k > mx) mx = k;
    }
    if (mn == mx) return;  // 键全相等，已经有序

    // 2. 下标映射：B 个下标覆盖 [mn, mx]。
    // scale 只算一次；uint64→double、乘正常数、截断都单调，
    // 所以 bucket_of 单调，同一下标对应连续值区间。下标是否均匀只影响速度。
    const std::uint64_t range = mx - mn;
    // max_buckets == 0 会让 B == 0，后面的 clamp 就会下溢成 SIZE_MAX；
    // 统一按 1 处理：每层只有一个下标，靠深度保护最终交给兜底，慢但正确。
    // 计数表要留在缓存里：n ≤ 2e6 用 64K 桶（256KB 表），
    // 更大的 n 用 1M 桶（4MB 表）。max_buckets 仍然可以再往下压。
    const std::size_t auto_cap =
        (m <= 2000000) ? (std::size_t(1) << 16) : (std::size_t(1) << 20);
    const std::size_t cap = (std::min)(opt.max_buckets, auto_cap);
    const std::size_t B =
        (std::max)(std::size_t(1), (std::min)(m, cap));
    const double scale = static_cast<double>(B) / static_cast<double>(range);

    auto bucket_of = [&](std::uint64_t k) -> std::size_t {
        const double t = static_cast<double>(k - mn) * scale;
        const std::uint64_t b = static_cast<std::uint64_t>(t);
        return b >= B ? B - 1 : static_cast<std::size_t>(b);
    };

    // 3. 计数：cnt[i] 是算出下标 i 的元素数。
    // uint32 够用：sort_by 已经拒绝了 n > 2^32-1，位置最大也是 2^32-1。
    std::vector<std::uint32_t> cnt(B, 0);
    for (std::size_t i = 0; i < m; ++i) {
        ++cnt[bucket_of(key(a[i]))];
    }

    // 4. 原地前缀和：计数变成每个组的起点。
    // 必须先存原来的计数，再写起点，否则累加会丢数。
    std::uint32_t running = 0;
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t c = cnt[b];
        cnt[b] = running;
        running += c;
    }

    // 5. 正向落位：cnt[i] 现在既是组起点，也是“下一个空位”的游标。
    // 每个槽位只写一次，结果是输入的一个置换；组内保持输入顺序。
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t b = bucket_of(key(a[i]));
        const std::uint32_t pos = cnt[b]++;
        buf[static_cast<std::size_t>(pos)] = a[i];
    }
    std::copy(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), a);

    // 6. 组内递归。填充结束后 cnt[i] 是组 i 的终点，
    // 所以组 i 的区间是 [(i == 0 ? 0 : cnt[i-1]), cnt[i])。
    for (std::size_t b = 0; b < B; ++b) {
        const std::uint32_t begin = (b == 0) ? 0 : cnt[b - 1];
        const std::uint32_t end = cnt[b];
        const std::uint32_t len = end - begin;
        if (len <= 1) continue;
        if (len < opt.leaf_sort) {
            // 叶子：严格小于 leaf_sort（默认 22），就地插入。
            insertion_sort(a + begin, static_cast<std::size_t>(len), key);
        } else if (len < opt.small_sort) {
            // [16, small_sort)：直接本地快排；默认 small_sort=16 时这一档为空。
            fallback(a + begin, a + end,
                     [&](const T& x, const T& y) { return key(x) < key(y); });
        } else {
            sort_range(a + begin, static_cast<std::size_t>(len), depth + 1,
                       key, fallback, opt, buf);
        }
    }
}

}  // namespace detail

// 公开的保序键映射（uint64_t）。sort_by 的调用方可以直接用，
// 也可以自己提供单调映射。
template <class T>
std::uint64_t key_of(const T& v) noexcept {
    return detail::to_key(v);
}

// XSort：调度器入口（项目专有名字）。判断交给兜底还是走 OblivoneSort 核心。
// fallback 在段太小或递归到顶时被调用，默认 std::sort。
template <class T, class KeyFn, class Fallback = default_fallback>
void xsort_by(T* data, std::size_t n, KeyFn key,
              const options& opt = {}, Fallback fallback = Fallback{}) {
    if (n < 2) return;
    if (n > 0xFFFFFFFFull) {  // 计数表是 32 位
        fallback(data, data + n, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }
    if (n <= opt.small_sort) {  // 总数 ≤16：不进 Oblivone
        fallback(data, data + n, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }

    // 已有序则直接退出；随机数据下两个标志几步内就会失效，几乎零成本。
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

    // 近有序快速路径：带预算的插入排序。
    // 完成就直接返回；超预算中止后，数据仍是置换，继续走 Oblivone。
    if (detail::budgeted_insertion(data, n, key, n / 8)) return;

    std::vector<T> buf(n);  // 整棵递归树只分配这一次
    detail::sort_range(data, n, 0, key, fallback, opt, buf);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void xsort_by(std::vector<T>& v, KeyFn key,
              const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort_by(v.data(), v.size(), key, opt, fallback);
}

// OblivoneSort 核心：跳过规模/有序性判断，直接分桶。
// 适合“已知乱序”的调用方；深度到顶仍会走兜底。
template <class T, class KeyFn, class Fallback = default_fallback>
void oblivone_sort(T* data, std::size_t n, KeyFn key,
                   const options& opt = {}, Fallback fallback = Fallback{}) {
    if (n < 2) return;
    if (n > 0xFFFFFFFFull) {
        fallback(data, data + n, [&](const T& x, const T& y) { return key(x) < key(y); });
        return;
    }
    std::vector<T> buf(n);
    detail::sort_range(data, n, 0, key, fallback, opt, buf);
}

// 算术键的便捷入口（int32/int64/uint32/uint64/float/double）。
template <class T, class Fallback = default_fallback>
void xsort(T* data, std::size_t n,
           const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort_by(data, n, [](const T& v) { return detail::to_key(v); }, opt, fallback);
}

template <class T, class Fallback = default_fallback>
void xsort(std::vector<T>& v,
           const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort(v.data(), v.size(), opt, fallback);
}

// 标准别名：sort / sort_by，按 std 的习惯调用。
template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(T* data, std::size_t n, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort_by(data, n, key, opt, fallback);
}

template <class T, class KeyFn, class Fallback = default_fallback>
void sort_by(std::vector<T>& v, KeyFn key,
             const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort_by(v, key, opt, fallback);
}

template <class T, class Fallback = default_fallback>
void sort(T* data, std::size_t n,
          const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort(data, n, opt, fallback);
}

template <class T, class Fallback = default_fallback>
void sort(std::vector<T>& v,
          const options& opt = {}, Fallback fallback = Fallback{}) {
    xsort(v, opt, fallback);
}

}  // namespace oblivone

#endif  // OBLIVONE_SORT_HPP

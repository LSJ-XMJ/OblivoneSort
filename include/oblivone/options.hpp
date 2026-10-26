#ifndef OBLIVONE_OPTIONS_HPP
#define OBLIVONE_OPTIONS_HPP

#include <cstddef>

namespace oblivone {

// 单线程排序参数。默认值来自低 n、标准模式与 B 扫描的重复实验。
struct options {
    // 顶层 n <= small_top 时直接交给 fallback。
    std::size_t small_top = 48;

    // 核心内部组 m < small_sort 时交给 fallback。
    std::size_t small_sort = 16;

    // 组 m < leaf_sort 时使用插入排序。
    std::size_t leaf_sort = 22;

    // 嵌套块 m > scan_threshold 时先做有序扫描；0 表示关闭。
    std::size_t scan_threshold = 256;

    // 递归深度上限；到顶后交给 fallback。
    int max_depth = 16;

    // 每层桶数硬上限；0 按 1 处理。
    std::size_t bucket_limit = 65536;
};

}  // namespace oblivone

#endif  // OBLIVONE_OPTIONS_HPP

# Changelog

## v1.1.0 — 2026-10-28

### Added

- 可选桶号缓存 `options::cache_bucket_index`，默认关闭。
- 单线程递归与并行 root 的缓存路径。
- `uint16_t` 桶号表；仅有效桶数 `<= 65536` 时生效。
- 缓存单元测试、差分 fuzz 和并行回退测试。

### Notes

- 额外约 `2n` 字节内存。
- 适合昂贵自定义 key；默认整数 key 不保证收益。
- `B > 65536` 自动忽略缓存，不支持 B=n 缓存。
- 默认行为与 1.0.x 完全一致。
- 强算法对比显示 TimSort、pdqsort 和 Boost 并行排序在部分场景更快；缓存与默认路径均不宣称全场景最优。

## v1.0.1 — 2026-10-28

文档与元数据补丁。

### Changed

- 添加 v1.0.0 中英文技术文档。
- 添加 Zenodo DOI。
- 忽略本地测试工作区与暂存文档。

## v1.0.0 — 2026-10-27

首个公开版本。

### Added

- C++17 header-only 单线程 `sort` / `sort_by`；
- 保序 `uint64_t` key 映射；
- double NaN 最后、`-0 == +0`；
- 顶层有序/逆序/近有序快速路径；
- 分桶、计数、前缀和、散播、递归、叶子插入；
- 默认 fallback：算术小段插入，其余 `std::sort`；
- 可选并行 `parallel_sort` / `parallel_sort_by`；
- 公开 `thread_pool`，支持注入；
- 并行 root count/scatter/copy 与 depth-1 组并行；
- `scratch_budget` 限制并行内存；
- CMake interface library、安装/导出、`find_package(oblivone)`；
- 差分 fuzz、并行并发测试、标准基准；
- 设计文档、参数文档、负结果记录。

### Defaults

```text
small_top      = 48
small_sort     = 16
leaf_sort      = 22
scan_threshold = 256
max_depth      = 16
bucket_limit   = 65536
```

并行：

```text
threads        = 0（自动）
min_n          = 2000000
max_buckets    = 65536
scratch_budget = 0（n * sizeof(T)）
pool           = nullptr
```

### Notes

- 不稳定排序；
- 不追求结构化数据，TimSort 在该场景更强；
- 不使用第三方运行时库；
- Apache-2.0，无专利；
- 当前 macOS 主要依赖 GitHub hosted runner，可能受容量影响。

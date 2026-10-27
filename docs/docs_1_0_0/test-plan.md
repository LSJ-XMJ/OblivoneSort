# 测试方案

## 1. 目标

验证：

1. 排序结果正确；
2. 边界输入不崩溃；
3. 并行路径无数据竞争/死锁；
4. 安装/导出可用；
5. 性能没有明显退化。

## 2. 测试层级

| 层级 | 内容 |
|---|---|
| 单元测试 | 小 n、边界、API、fallback、NaN |
| 差分 fuzz | 随机输入 + 随机参数，对比 `std::sort` |
| 并行测试 | 多线程、注入池、并发调用 |
| 集成测试 | CMake 构建、ctest、install、consumer |
| 平台测试 | Linux / Windows / macOS CI |
| 性能测试 | 标准模式、并行规模 |

## 3. 单元测试

`tests/test_sort.cpp` 覆盖：

- double random 100k；
- double sorted/reverse；
- double 全相等 50k；
- double NaN、±Inf、-0；
- int32 random 100k；
- int64 极值；
- uint64 极值；
- float NaN；
- options 边界：
  - `small_top=0`
  - `small_sort=0`
  - `leaf_sort=0`
  - `scan_threshold=0`
  - `max_depth=3`
  - `bucket_limit=1`
- 自定义 key `sort_by`；
- 自定义 fallback 被调用；
- 小 n。

校验：

1. 按 key 非递减；
2. 与参考排序的 key 多重集合一致；
3. NaN 在最后；
4. fallback 调用次数非零。

## 4. 差分 fuzz

`tests/fuzz_sort.cpp`：

- 类型：double / float / int32 / uint32；
- 规模：0..1999；
- 分布：
  - 均匀
  - 正态
  - 指数
  - 少值
  - 全相等
  - 已排序
  - 逆序
- 随机 options：
  - small_top 0..128
  - small_sort 0..24
  - leaf_sort 0..32
  - scan_threshold 0..512
  - max_depth 1..12
  - bucket_limit 0..4096
- 参考：同 key 的 `std::sort`；
- 校验：
  - 有序；
  - 多重集合 key 一致。

目标：

```text
累计 ≥ 200 万组
```

## 5. 并行测试

`tests/test_parallel.cpp` 覆盖：

- random / sorted / reverse / equal / small / empty；
- threads=1/2/4；
- 注入 `thread_pool`；
- 自定义 key；
- 4 个调用各自建池并发；
- 2 个调用共享一个注入池；
- 40 组随机小规模并行。

校验：

- 每个结果按 key 有序；
- 不崩溃、不死锁；
- 注入池可重复使用。

## 6. 集成测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

安装：

```bash
cmake --install build --prefix build/install
```

consumer：

- `find_package(oblivone CONFIG REQUIRED)`；
- 单线程目标 `oblivone::oblivone`；
- 并行目标 `oblivone::parallel`；
- 编译并运行。

## 7. 平台

CI：

- Linux；
- Windows；
- macOS（受 GitHub runner 容量影响）。

工具链：

- GCC；
- Clang；
- MSVC。

## 8. 性能测试

### 单线程

`bench/bench_standard.cpp`：

- random / sorted / reverse / organ / few / near / equal；
- n=1e6；
- 与 `std::sort` 同机对比。

### 并行

`bench/bench_parallel.cpp`：

- n=1e6/1e7/2e7；
- threads=4/12；
- 串行 vs 并行；
- 校验结果有序。

记录：

- 绝对 ms；
- 相对比；
- 机器/编译器/优化选项。

## 9. 通过标准

- ctest 3/3；
- fuzz ≥200 万组；
- 并行并发测试通过；
- install/consumer 通过；
- CI 三平台绿（macOS 受 runner 可用性影响）；
- 性能在回归线内。

## 10. 未覆盖/后续

- TSan；
- ASan/UBSan 全量；
- 多机器性能；
- 1e8+ 规模；
- 故意抛异常的 key/fallback；
- fork/进程级并发。

# 测试记录

记录日期：2026-10-27  
项目版本：1.0.0

## 1. 环境

| 项 | 值 |
|---|---|
| OS | Windows |
| CPU | i5-13500H，16 逻辑核 |
| 构建 | CMake Release |
| 编译器 | MSVC / GCC 15.2 |
| 优化 | Release / `-O2` |

绝对时间受机器、频率、热漂移影响，只作为本机记录，不作为跨机器承诺。

## 2. 正确性测试

### 2.1 ctest

```bash
ctest --test-dir build -C Release --output-on-failure
```

结果：

```text
1/3 test_sort ......... Passed
2/3 fuzz_sort ......... Passed
3/3 test_parallel ..... Passed

100% tests passed, 0 tests failed out of 3
```

`test_parallel` 单次约 0.99 s。

### 2.2 fuzz

```bash
fuzz_sort 1000000 2026
```

结果：

```text
fuzz passed: 1000000 cases, seed=2026
```

此前同版本还执行过：

```text
seed=1     1,000,000 cases
seed=42    1,000,000 cases
seed=7     1,000,000 cases
```

累计约 400 万组。

### 2.3 并行重复

```text
test_parallel 连续 100 次
结果：全部通过
耗时：约 12.34 s
```

未发现死锁、崩溃、结果错误。

### 2.4 安装/导出

执行：

```bash
cmake --install build --prefix build/install-v1
```

consumer：

```cmake
find_package(oblivone CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE oblivone::parallel)
```

结果：

```text
consumer exit=0
```

单线程头、并行头、包配置均可用。

## 3. 单线程基准

### 3.1 MSVC，1e6

`bench_standard 1000000 3`：

| 模式 | std::sort | Oblivone | 比值 |
|---|---:|---:|---:|
| random | 112.318 ms | 40.760 ms | 0.363 |
| sorted | 29.290 ms | 3.710 ms | 0.127 |
| reverse | 34.117 ms | 4.252 ms | 0.125 |
| organ | 63.451 ms | 26.267 ms | 0.414 |
| few | 17.840 ms | 19.630 ms | 1.100 |
| near 0.1% | 29.279 ms | 24.567 ms | 0.839 |
| equal | 3.362 ms | 4.263 ms | 1.268 |

观察：

- random/sorted/reverse/organ 明显快于 std；
- few 和 equal 在该编译器/规模下略慢；
- 这些结果说明实现不是所有场景都占优。

### 3.2 GCC，1e6

同版本早前记录：

| 模式 | std::sort | Oblivone | 比值 |
|---|---:|---:|---:|
| random | 81.595 ms | 23.040 ms | 0.282 |
| sorted | 31.345 ms | 1.643 ms | 0.052 |
| organ | 97.688 ms | 18.339 ms | 0.188 |
| few | 34.771 ms | 12.676 ms | 0.365 |

GCC 与 MSVC 的绝对值和相对值不同，说明性能对编译器敏感；后续需要分编译器标定。

## 4. 并行基准

### 4.1 MSVC，1e7

`bench_parallel 10000000 2 12 2000000`：

| 项 | 时间 |
|---|---:|
| 串行 | 439.437 ms |
| 并行 T=12 | 101.818 ms |
| 比值 | 0.232 |

### 4.2 GCC，1e7/2e7

同版本早前记录：

| n | 串行 | 并行 T=12 | 比值 |
|---:|---:|---:|---:|
| 1e7 | 258.277 ms | 87.723 ms | 0.340 |
| 2e7 | 524.158 ms | 176.218 ms | 0.336 |

观察：

- 并行在大 n 有稳定收益；
- 1e6 few/organ 可能回退，所以默认 `min_n=2e6`；
- 并行绝对收益受内存带宽和线程数影响。

## 5. CI

已执行：

- Linux：绿；
- Windows：绿；
- macOS：绿（曾受 GitHub macOS arm64 runner 容量影响，属于基础设施，不是测试失败）。

CI 内容：

```text
configure
build
ctest
fuzz 200000
```

## 6. 未执行

以下尚未形成完整记录：

- TSan；
- ASan/UBSan 全量；
- 1e8+ 规模；
- 多机器基准；
- 故意抛异常的 key/fallback；
- 进程级并发。

这些属于后续验证项，不影响当前已记录的正确性结论。

## 7. 结论

在当前环境与测试集下：

- 正确性测试通过；
- 并行并发测试通过；
- 安装/导出通过；
- 大 n 随机数据有并行收益；
- 小 n 结构化数据、few、equal 不保证优于 `std::sort`。

本记录只说明“在这些条件下通过”，不代表所有平台、所有分布都最优。

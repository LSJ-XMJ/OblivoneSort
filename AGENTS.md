# OblivoneSort — 项目约定（给 AI 协作者）

> 本文件在每次会话自动加载。详细任务交接见 `docs/stop/HANDOFF-2.0.0.md`（本地内部文档，gitignored，grep 可见）。
> **用户最新指令优先于本文件。**

## 项目

- C++17、header-only、**非稳定**分布排序；Apache-2.0；无第三方运行时依赖。
- 作者：肖明杰；仓库：<https://github.com/LSJ-XMJ/OblivoneSort>。
- 当前版本：`2.0.1`（发布准备中；tag `v2.0.0` 已发布；DOI `10.5281/zenodo.23675733`，新版 version DOI 发布后回填）。
- 定位：random / smooth 数值分布强；organ / push / few / sorted 落后，不要宣称全场景最快。

## 架构边界

- `include/oblivone/sort.hpp`：串行公开入口（`sort` / `sort_by` / `xsort` / `xsort_by` / `key_of`）。
- `include/oblivone/detail/`：串行核心（key、dispatch、core、fallback、coarse）。
- `include/oblivone/parallel.hpp`：**正式**并行 API（`thread_pool` / `parallel_sort`）；唯一允许依赖 `<thread>` 的公共头。
- `include/oblivone/advanced/`：实验调度器，**不安装、非公开 API**；不要提升为正式接口。
- `test_myself/`：本地实验区，gitignored；不是发布源码，**禁止把其中代码直接复制进公开实现**。
- `docs/docs_2_0_1{,_EN}/`：发布文档；`docs/stop/`：内部文档（gitignored，不进云端）。

## 硬约束

1. 串行核心不依赖线程；线程只能在 `parallel.hpp` / `advanced/`。
2. 语义不变：不稳定；NaN 最后；`-0 == +0`；额外 `n` 元素空间；`n > UINT32_MAX` 直接 fallback。
3. 不引入第三方运行时依赖；Boost / TimSort / pdqsort 只允许出现在 bench。
4. `oblivone::advanced` 不进安装包（`install(DIRECTORY ... PATTERN "advanced" EXCLUDE)`），不要改。
5. 已发布 tag 不移动；重打 tag 必须用户明确同意。需要新元数据时发新 patch 版本。
6. **Git 只读（用户 2026-10-31 指令）**：禁止一切 git 写/改/删——不 commit、不 push、不 tag、不 fetch/pull/merge/branch/checkout 改状态；已跟踪文件也不改。发现问题只写报告/给命令建议，由用户自己执行。
7. 中文文档权威；英文文档带“翻译可能有误，以中文为准”的声明。
8. 文档只写功能、设计、使用、测试数据；不写对话、约定、未证实结论。
9. 性能结论必须带机器/编译器/n/线程/模式/重复次数；失败实验如实记录。
10. 内部工作文档放 `docs/stop/`，不要提交到公开仓库。

## 验证

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

- 期望 `ctest 8/8`；2026-10-31 已在本机 GCC 15.2 验证。
- CI：`.github/workflows/ci.yml`（3 OS + ASan/UBSan）、`tsan.yml`（Ubuntu TSan，push 含 `main`）。
- Windows 原生不支持 TSan；TSan 只能在 Linux/WSL/GitHub 跑。
- 旧 `build-release/` 是从旧路径复制的，CMake 缓存已失效，不要用它判断结果。

## 交接要点

- 完整历史、已放弃方向、待办与已知问题：`docs/stop/HANDOFF-2.0.0.md`。
- 发布数据：`docs/docs_2_0_1/test-record.md` + `test-data/*.csv`。
- 强外部基线复现：`test_myself/bench/bench_docs.cpp`（需 Boost 1.89 与 TimSort 头，本地）。
- 当前发布页待核对：GitHub Release 资产数量、Zenodo 2.0.1 的 version DOI（发布后回填 CITATION）。

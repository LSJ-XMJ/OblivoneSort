# OblivoneSort 1.1.0 技术文档

OblivoneSort 1.1.0 在 1.0.x 基础上增加了可选的桶号缓存，默认关闭。

| 文档 | 内容 |
|---|---|
| [cache.md](cache.md) | 桶号缓存的 API、限制、内存和性能 |

## 当前版本

```text
OblivoneSort 1.1.0
C++17 / header-only
Apache-2.0
```

## 1.0.x 文档

1.0.x 的完整设计、实现和测试文档仍在：

```text
docs/docs_1_0_0/
docs/docs_1_0_0_EN/
```

1.1.0 的算法默认路径与 1.0.x 相同；新增缓存是显式 opt-in。

# 贡献指南

感谢你考虑参与 Numotirus。这不是“帮我们”的项目，是“和我们一起建”的项目。

*备注：无 **CMake** 方案。*

## 当前进度

| 模块 | 状态 | 说明 |
| ------ | ------ | ------ |
| `core/crypto` | ✅ 已完成 | C API，X25519 + XChaCha20-Poly1305 + ECIES |
| `core/p2p` | ✅ 已完成 | UDP + KCP + select 非阻塞，跨平台 |
| `core/protocol` (Noise) | ✅ 已完成 | Noise XX 握手 + SAS 生成 + TOFU |
| `p2p_chat.c` | ✅ 可运行 | 命令行聊天示例 |
| `core/plugin` | ❌ 待实现 | 插件系统 |
| GUI | ❌ 待实现 | Avalonia (C#) |

**下一步需要你**：NAT 穿透、GUI 原型、神霁集成。

## Issue 提交

### Bug 报告

- 复现步骤
- 预期行为
- 实际行为
- 环境（操作系统、编译器版本）

### 功能请求

- 用户场景（不是“我要 X 功能”，而是“我想做 Y，X 能帮我”）
- 说明是哪一层：加密层 / 网络层 / 协议层 / GUI

## Pull Request

### 分支命名

- `feature/简短描述`
- `fix/简短描述`
- `doc/简短描述`

### PR 标题格式

`[层名] 简短描述`

示例：

- `[Crypto] 添加 X25519 密钥交换骨架`
- `[Network] 添加 KCP 超时重传优化`
- `[Protocol] Noise 协议实现`
- `[GUI] 初始化 Avalonia 主窗口`

### PR 描述

如果有，就必须包含：`Closes #(issue 编号)`

### 审核要求（分阶段）

| 阶段 | 活跃贡献者人数 | 审核要求 |
| ------ | ---------------- | ---------- |
| 初期 | < 3 人 | 无需批准，作者自己 squash merge |
| 中期 | 3-10 人 | 至少 1 名**其他**贡献者批准 |
| 成熟期 | 10+ 人 | 至少 2 人批准 |

### 合并策略

仅允许 squash merge。保持提交历史干净。

## 代码风格

核心语言：**C11 + C++20**（C 部分用 C11，协议层用 C++20）

- C/C++ 代码：提交前运行 `clang-format`
- C# 代码：提交前运行 `dotnet format`
- 代码注释：英文在前，中文在后，格式 `// English text. 中文文本。`
- AI 生成代码：允许，但必须理解每一行、自己测试、PR 中声明

完整规范见 [`CODING_STYLE.md`](docs/CODING_STYLE.md)

## 行为准则

尊重他人。技术分歧不是人身攻击。

歧视、骚扰、人肉搜索 → 直接封禁。

## 问题咨询

在 GitHub 上开 Discussion，或在 [Discord](https://discord.com/invite/KdjnEtpSP8) 找我们。

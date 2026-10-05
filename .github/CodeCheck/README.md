# 代码质量检查

本项目使用两种方式自动检查 C11 / C++20 代码质量。

---

## 方案一：GitHub Actions（CI/CD）

每次 `push` 或 `pull_request` 时自动运行，在远程环境中检查项目代码。

**优点：**

- 远程运行，无需本地安装
- 所有 PR 都可以自动检查
- 检查结果在 PR 页面可见

**配置文件：** `.github/workflows/code-quality.yml`

在运行 CodeCheck 前，GitHub Actions 会自动扫描 `core/` 下的 `.cpp` 文件，并生成 `compile_commands.json`。

---

## 方案二：Pre-commit 本地钩子

如果安装了 Git Hook，则会在每次 commit 前在本地运行，能够在 push 之前即时发现问题。

**优点：**

- 提交前即时反馈
- 节省 CI 资源
- 可配合 IDE 使用

Pre-commit **不一定要安装为 Git Hook**，也可以在需要时手动运行。

---

## 安装 Pre-commit

### 1. 安装 Python 依赖

```bash
pip install pre-commit
```

### 2. 安装 Git 钩子

```bash
pre-commit install
```

安装后，每次创建 commit 时，Pre-commit 都会自动运行。

### 3. （可选）安装 pre-push 钩子

```bash
pre-commit install --hook-type pre-push
```

---

## 手动运行检查

### 检查所有文件

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml --all-files
```

### 只检查暂存文件

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml
```

### 只检查特定文件

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml --files core/crypto/crypto.cpp
```

### 生成 compile_commands.json

```bash
python3 .github/CodeCheck/scripts/generate_compile_command.py
```

该脚本会扫描 `core/` 下的 `.cpp` 文件，并更新：

```text
.github/CodeCheck/compile_commands.json
```

第三方依赖以及被排除的目录不会加入 compilation database。

---

## 跳过检查（紧急情况使用）

```bash
git commit --no-verify
```

或：

```bash
git commit -n
```

这两个选项会跳过本地 Git Hooks，包括已经安装的 Pre-commit Hook。

**它们不会跳过 GitHub Actions。**

如果没有安装 Pre-commit Git Hook，则 `git commit` 本身不会自动运行 Pre-commit，因此自然也就没有本地检查需要跳过。

---

## 检查项说明

| 检查项 | 工具/脚本 | 说明 |
| ------ | --------- | ---- |
| C++ 静态分析 | `clang-tidy` | C++ 静态分析及现代 C++ 检查 |
| 注释规范 | `check_comment_style.py` | 检查注释规范及公共接口文档 |
| L10N 占位符 | `check_l10n.py` | 检查 `// * L10N_PENDING [...] *` 占位符格式 |
| 第三方依赖 | `check_dependencies.py` | 扫描项目文件中的第三方依赖使用情况 |
| 基础文件格式 | `trailing-whitespace` | 清除行尾多余空白 |
| 文件结尾格式 | `end-of-file-fixer` | 确保文件结尾具有正确的换行 |
| YAML 语法 | `check-yaml` | 检查 YAML 文件语法 |
| 大文件检查 | `check-added-large-files` | 防止意外添加过大的文件 |
| 私钥检查 | `detect-private-key` | 检测可能被意外提交的私钥 |
| 合并冲突 | `check-merge-conflict` | 检测合并冲突标记 |

当前 CodeCheck 配置**不使用 `clang-format`**。

---

## 常见问题

### Q: 为什么我的提交被阻止了？

A: 如果已经将 Pre-commit 安装为 Git Hook，则一个或多个检查可能会在 commit 创建之前失败。查看终端输出中的具体错误信息，修复问题后重新提交。

如果某个 hook 自动修改了文件，请检查这些修改，然后重新运行检查。

### Q: 如何查看具体的错误信息？

A: 错误信息会显示在终端中，并指出对应的文件和检查项目。适用的检查会提供中英双语提示。

### Q: 团队成员需要各自安装吗？

A: 不需要。GitHub Actions 不要求任何人进行本地安装。

如果开发者希望在每次 commit 前自动检查，可以在本地执行一次 `pre-commit install`。

也可以不安装 Git Hook，而是在需要时手动运行 Pre-commit。

### Q: 可以在 Windows 上使用吗？

A: 可以。CodeCheck 脚本使用 Python 3 编写，并设计为跨平台运行。

如果需要运行本地检查，请确保已安装 Python 3、Git 和 Pre-commit。

---

**文档版本**：1.0

**语言标准**：C11 + C++20

**最后更新**：2026-10-05

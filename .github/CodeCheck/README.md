# 代码质量检查

本项目使用两种方式自动检查 C11 / C++20 代码规范。

---

## 方案一：GitHub Actions（CI/CD）

每次 `push` 或 `pull_request` 时自动运行，强制检查所有贡献者的代码。

**优点：**

- 远程运行，无需本地安装
- 所有 PR 自动检查，不通过无法合并
- 检查结果在 PR 页面可见

**配置文件：** `.github/workflows/code-quality.yml`

---

## 方案二：Pre-commit 本地钩子

提交代码前在本地运行，即时反馈，避免提交后再等 CI 报错。

**优点：**

- 提交前即时反馈
- 节省 CI 资源
- 可配合 IDE 使用

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

### 3. （可选）安装 pre-push 钩子

```bash
pre-commit install --hook-type pre-push
```

---

## 手动运行检查

### 检查所有文件

```bash
pre-commit run --all-files
```

### 只检查暂存文件

```bash
pre-commit run
```

### 只检查特定文件

```bash
pre-commit run --files core/crypto/crypto.c
```

---

## 跳过检查（紧急情况使用）

```bash
git commit --no-verify
```

或

```bash
git commit -n
```

---

## 检查项说明

| 检查项 | 工具/脚本 | 说明 |
| ------ | --------- | ---- |
| 代码格式化 | `clang-format` | 基于 Google 风格，缩进 4 空格，列宽 100 |
| C++ 静态分析 | `clang-tidy` | 检查 C++ 核心准则、现代 C++ 写法 |
| C 静态分析 | `cppcheck` | 检查 C 代码中的潜在 bug |
| C 编码规范 | `check_c_coding_style.py` | 错误码约定、内存管理、include 顺序、命名 |
| C++ 编码规范 | `check_cpp_coding_style.py` | 命名规范、禁止项检查 |
| 注释规范 | `check_comment_style.py` | 公共接口必须有注释 |
| L10N 占位符 | `check_l10n.py` | `// * L10N_PENDING [...] *` 格式检查 |
| AI 使用声明 | `check_ai_declaration.py` | PR 描述中声明 AI 生成代码 |

---

## 常见问题

### Q: 为什么我的提交被阻止了？

A: 检查脚本发现了不符合规范的代码。查看终端输出中的具体错误信息，修复后重新提交。

### Q: 如何查看具体的错误信息？

A: 错误信息会在终端中彩色显示，包含文件名、行号和具体描述。中英双语提示。

### Q: 团队成员需要各自安装吗？

A: 是的，每个开发者的本地环境需要执行一次 `pre-commit install`。GitHub Actions 不需要任何人安装。

### Q: 可以在 Windows 上使用吗？

A: 可以。脚本使用 Python 3 编写，跨平台兼容。确保已安装 Python 3 和 Git Bash。

---

**文档版本**：1.0

**语言标准**：C11 + C++20

**最后更新**：2026-06-13

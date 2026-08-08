# Code Quality Checks

This project uses two methods to automatically check C11 / C++20 code style compliance.

---

## Option A: GitHub Actions (CI/CD)

Automatically runs on every `push` or `pull_request`, enforcing style rules on all contributors.

**Advantages:**

- Runs remotely, no local installation required
- All PRs automatically checked, cannot merge if failing
- Check results visible on PR page

**Configuration:** `.github/workflows/code-quality.yml`

---

## Option B: Pre-commit Local Hooks

Runs locally before each commit, providing instant feedback and avoiding waiting for CI failures after push.

**Advantages:**

- Instant feedback before commit
- Saves CI resources
- Can be integrated with IDE

---

## Installing Pre-commit

### 1. Install Python dependency

```bash
pip install pre-commit
```

### 2. Install Git hooks

```bash
pre-commit install
```

### 3. (Optional) Install pre-push hooks

```bash
pre-commit install --hook-type pre-push
```

---

## Manual Execution

### Check all files

```bash
pre-commit run --all-files
```

### Check only staged files

```bash
pre-commit run
```

### Check specific files

```bash
pre-commit run --files core/crypto/crypto.c
```

---

## Skipping Checks (Emergency Use)

```bash
git commit --no-verify
```

or

```bash
git commit -n
```

---

## Check Items

| Check Item | Tool / Script | Description |
| ---------- | ------------- | ----------- |
| Code formatting | `clang-format` | Google style, 4-space indent, 100-column limit |
| C++ static analysis | `clang-tidy` | C++ Core Guidelines, modern C++ idioms |
| C static analysis | `cppcheck` | Potential bugs in C code |
| C coding style | `check_c_coding_style.py` | Error codes, memory management, include order, naming |
| C++ coding style | `check_cpp_coding_style.py` | Naming conventions, forbidden items |
| Comment style | `check_comment_style.py` | Public interfaces must have comments |
| L10N placeholders | `check_l10n.py` | `// * L10N_PENDING [...] *` format |
| AI usage declaration | `check_ai_declaration.py` | Declare AI-generated code in PR description |

---

## FAQ

### Q: Why was my commit blocked?

A: The check scripts found non-compliant code. Review the specific error messages in the terminal output, fix the issues, and retry the commit.

### Q: How can I view detailed error messages?

A: Error messages are displayed in color in the terminal, including file name, line number, and specific description. Bilingual (EN/CN) prompts are provided.

### Q: Do team members need to install locally?

A: Yes, each developer needs to run `pre-commit install` once on their local environment. GitHub Actions requires no installation from anyone.

### Q: Can I use this on Windows?

A: Yes. Scripts are written in Python 3 and are cross-platform compatible. Ensure Python 3 and Git Bash are installed.

---

**Document Version**：1.0

**Language Standard**：C11 + C++20

**Last Updated**：2026-06-13

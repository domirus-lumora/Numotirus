# Code Quality Checks

This project uses two methods to automatically check C11 / C++20 code quality.

---

## Option A: GitHub Actions (CI/CD)

Automatically runs on every `push` or `pull_request`, checking the project code remotely.

**Advantages:**

- Runs remotely, no local installation required
- All PRs can be checked automatically
- Check results are visible on the PR page

**Configuration:** `.github/workflows/code-quality.yml`

Before running CodeCheck, GitHub Actions automatically generates `compile_commands.json` by scanning the `.cpp` files under `core/`.

---

## Option B: Pre-commit Local Hooks

Runs locally before each commit if Git hooks are installed, providing instant feedback before pushing code.

**Advantages:**

- Instant feedback before commit
- Saves CI resources
- Can be integrated with IDE

Pre-commit does **not** have to be installed as a Git hook. It can also be run manually when needed.

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

After installation, Pre-commit will run automatically when creating a commit.

### 3. (Optional) Install pre-push hooks

```bash
pre-commit install --hook-type pre-push
```

---

## Manual Execution

### Check all files

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml --all-files
```

### Check only staged files

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml
```

### Check specific files

```bash
pre-commit run --config .github/CodeCheck/.pre-commit-config.yaml --files core/crypto/crypto.cpp
```

### Generate compile_commands.json

```bash
python3 .github/CodeCheck/scripts/generate_compile_command.py
```

This scans the `.cpp` files under `core/` and updates:

```text
.github/CodeCheck/compile_commands.json
```

Third-party and excluded directories are not included in the compilation database.

---

## Skipping Checks (Emergency Use)

```bash
git commit --no-verify
```

or

```bash
git commit -n
```

These options skip local Git hooks, including the Pre-commit hook if it has been installed.

They do **not** skip GitHub Actions.

If the Pre-commit Git hook has not been installed, `git commit` will not automatically run Pre-commit, so there is nothing to skip locally.

---

## Check Items

| Check Item | Tool / Script | Description |
| ---------- | ------------- | ----------- |
| C++ static analysis | `clang-tidy` | C++ static analysis and modern C++ checks |
| Comment style | `check_comment_style.py` | Checks comment style and public interface documentation |
| L10N placeholders | `check_l10n.py` | Checks `// * L10N_PENDING [...] *` placeholder format |
| Third-party dependencies | `check_dependencies.py` | Scans project files for third-party dependency usage |
| Basic file formatting | `trailing-whitespace` | Removes trailing whitespace |
| End-of-file formatting | `end-of-file-fixer` | Ensures files have a correct final newline |
| YAML syntax | `check-yaml` | Checks YAML file syntax |
| Large files | `check-added-large-files` | Prevents accidentally adding oversized files |
| Private keys | `detect-private-key` | Detects accidentally committed private keys |
| Merge conflicts | `check-merge-conflict` | Detects merge-conflict markers |

`clang-format` is **not used** by the current CodeCheck configuration.

---

## FAQ

### Q: Why was my commit blocked?

A: If Pre-commit is installed as a Git hook, one or more checks may fail before the commit is created. Review the specific error messages in the terminal output, fix the issues, and retry the commit.

If a hook automatically modifies files, review the changes and run the checks again.

### Q: How can I view detailed error messages?

A: Error messages are displayed in the terminal and identify the corresponding file and check. Bilingual (EN/CN) prompts are provided where applicable.

### Q: Do team members need to install locally?

A: No. GitHub Actions requires no local installation.

Developers who want automatic checks before every commit can run `pre-commit install` once in their local environment.

Pre-commit can also be run manually without installing the Git hook.

### Q: Can I use this on Windows?

A: Yes. The CodeCheck scripts are written in Python 3 and are designed to be cross-platform compatible.

Ensure Python 3, Git, and Pre-commit are installed if you want to run the local checks.

---

**Document Version**: 1.0

**Language Standard**: C11 + C++20

**Last Updated**: 2026-10-05

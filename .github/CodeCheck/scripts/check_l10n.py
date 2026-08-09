#!/usr/bin/env python3
"""
L10N_PENDING placeholder format checker. L10N_PENDING 占位符格式检查器。
Format: // * L10N_PENDING [作者-YYYYMMDD:HHMMSS] *
格式：// * L10N_PENDING [作者-YYYYMMDD:HHMMSS] *
"""

import re
import sys
import subprocess
import os


def get_project_root():
    """Get the project root directory. 获取项目根目录。"""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--show-toplevel'],
            capture_output=True, text=True,
            cwd=os.path.dirname(os.path.abspath(__file__))
        )
        root = result.stdout.strip()
        if root:
            return root
    except Exception:
        pass

    current = os.path.dirname(os.path.abspath(__file__))
    for _ in range(10):
        if os.path.exists(os.path.join(current, '.git')):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent

    return os.getcwd()


def get_staged_files(project_root):
    """Get staged files. 获取暂存的文件。"""
    try:
        result = subprocess.run(
            ['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
            capture_output=True, text=True,
            cwd=project_root
        )
        return [f for f in result.stdout.splitlines() if f.endswith(('.c', '.h', '.cpp', '.hpp'))]
    except Exception:
        return []


def check_l10n_format(content, filename):
    """Check L10N_PENDING format. 检查 L10N_PENDING 占位符格式。"""
    errors = []
    pattern = r'// \* L10N_PENDING \[[A-Z]+-\d{8}:\d{6}\] \*'
    lines = content.split('\n')

    for i, line in enumerate(lines):
        if 'L10N_PENDING' in line:
            if not re.match(pattern, line.strip()):
                errors.append(
                    f"  ❌ Line {i+1}: Invalid L10N_PENDING format. "
                    f"Expected: '// * L10N_PENDING [作者-YYYYMMDD:HHMMSS] *'. "
                    f"第 {i+1} 行：L10N_PENDING 格式错误，应为 '// * L10N_PENDING [作者-YYYYMMDD:HHMMSS] *'。"
                )

    return errors


def main():
    """Main entry point. 主入口。"""
    project_root = get_project_root()
    try:
        os.chdir(project_root)
    except Exception:
        pass

    files = get_staged_files(project_root)
    if not files:
        print("✅ No staged files. 没有暂存的文件。")
        return 0

    has_error = False

    for fname in files:
        full_path = os.path.join(project_root, fname)
        try:
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception:
            continue

        errors = check_l10n_format(content, fname)

        if errors:
            has_error = True
            print(f"\n📄 {fname}:")
            for err in errors:
                print(err)

    if has_error:
        print("\n❌ L10N_PENDING format check failed. L10N_PENDING 格式检查未通过。")
        sys.exit(1)
    else:
        print("✅ L10N_PENDING format check passed. L10N_PENDING 格式检查通过。")
        sys.exit(0)


if __name__ == '__main__':
    main()

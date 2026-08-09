#!/usr/bin/env python3
"""
Comment style checker. 注释规范检查器。
Checks that public interfaces (functions, classes, structs) have comments.
检查公共接口（函数、类、结构体）是否有注释。
"""

import re
import sys
import subprocess
import os
from typing import List


def get_staged_files() -> List[str]:
    """Get staged C/C++ files. 获取暂存的 C/C++ 文件。"""
    try:
        result = subprocess.run(
            ['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
            capture_output=True, text=True
        )
        return [f for f in result.stdout.splitlines()
                if f.endswith(('.c', '.h', '.cpp', '.hpp'))]
    except Exception:
        return []


def has_comment_before(lines: List[str], idx: int, max_lines: int = 3) -> bool:
    """
    Check if there is a comment within `max_lines` before `idx`.
    检查在 `idx` 之前 `max_lines` 行内是否有注释。
    """
    for j in range(max(0, idx - max_lines), idx):
        stripped = lines[j].strip()
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.endswith('*/'):
            return True
        # Multi‑line comment continuation
        if '/*' in stripped or '*/' in stripped:
            return True
    return False


def check_file(content: str, filename: str) -> List[str]:
    """Check a single file for missing comments. 检查单个文件是否缺少注释。"""
    errors = []
    lines = content.split('\n')
    # Remove C‑style block comments for easier detection (but we also check comments)
    # We'll iterate lines and detect comments as we go.
    for i, line in enumerate(lines):
        stripped = line.strip()
        # Skip empty, preprocessor, and #include lines
        if not stripped or stripped.startswith('#'):
            continue

        # Detect function declaration: return type + name + (params) + { or ;
        # This regex is conservative; it matches most C/C++ function declarations.
        if re.match(r'^(extern\s+)?[a-zA-Z_][\w\s\*]+\s+[a-zA-Z_]\w*\s*\([^)]*\)\s*(const\s*)?(override\s*)?(final\s*)?(;\s*$|\s*\{)', stripped):
            if not has_comment_before(lines, i):
                errors.append(f"  ❌ Line {i+1}: Public function declaration missing comment.")

        # Detect class/struct definition
        if re.match(r'^(class|struct)\s+[a-zA-Z_]\w*\s*(\{|\s*:)', stripped):
            if not has_comment_before(lines, i):
                errors.append(f"  ❌ Line {i+1}: Class/struct definition missing comment.")

    return errors


def main():
    """Main entry point. 主入口。"""
    # Get project root
    try:
        root = subprocess.run(['git', 'rev-parse', '--show-toplevel'],
                              capture_output=True, text=True).stdout.strip()
    except Exception:
        root = os.getcwd()
    os.chdir(root)

    files = get_staged_files()
    if not files:
        print("✅ No staged C/C++ files. 没有暂存的 C/C++ 文件。")
        return 0

    has_error = False
    for fname in files:
        full_path = os.path.join(root, fname)
        try:
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception:
            continue

        errors = check_file(content, fname)
        if errors:
            has_error = True
            print(f"\n📄 {fname}:")
            for err in errors:
                print(err)

    if has_error:
        print("\n❌ Comment style check failed. Please add comments to public interfaces. "
              "注释规范检查未通过，请为公共接口添加注释。")
        sys.exit(1)
    else:
        print("✅ Comment style check passed. 注释规范检查通过。")
        sys.exit(0)


if __name__ == '__main__':
    main()
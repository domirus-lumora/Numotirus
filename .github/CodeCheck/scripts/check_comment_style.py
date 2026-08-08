#!/usr/bin/env python3
"""
Comment style checker. 注释规范检查器。
Checks: public interfaces (functions, classes, structs) must have comments.
检查项：公共接口（函数、类、结构体）必须有注释。
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


def check_public_interface_comments(content, filename):
    """Check public interfaces have comments. 检查公共接口是否有注释。"""
    errors = []
    lines = content.split('\n')

    func_decl = re.compile(r'^[a-zA-Z_][\w\s\*]+\s+([a-zA-Z_]\w*)\s*\([^)]*\)\s*;')
    class_decl = re.compile(r'^class\s+([a-zA-Z_]\w*)\s*\{')
    struct_decl = re.compile(r'^struct\s+([a-zA-Z_]\w*)\s*\{')

    for i, line in enumerate(lines):
        stripped = line.strip()

        if func_decl.match(stripped):
            has_comment = False
            for j in range(max(0, i - 3), i):
                if '//' in lines[j] or '/*' in lines[j]:
                    has_comment = True
                    break
            if not has_comment:
                errors.append(
                    f"  ❌ Line {i+1}: Public function declaration missing comment. "
                    f"第 {i+1} 行：公共函数声明缺少注释。"
                )

        if class_decl.match(stripped) or struct_decl.match(stripped):
            has_comment = False
            for j in range(max(0, i - 3), i):
                if '//' in lines[j] or '/*' in lines[j]:
                    has_comment = True
                    break
            if not has_comment:
                errors.append(
                    f"  ❌ Line {i+1}: Class/struct definition missing comment. "
                    f"第 {i+1} 行：类/结构体定义缺少注释。"
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

        errors = check_public_interface_comments(content, fname)

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
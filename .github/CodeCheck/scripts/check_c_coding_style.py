#!/usr/bin/env python3
"""
C11 code style checker. C11 代码规范检查器。
Checks: error handling, memory management, include order, naming, header guards.
检查项：错误处理、内存管理、include 顺序、命名、头文件保护。
"""

import re
import sys
import subprocess
import os
from pathlib import Path


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
    """Get staged C files. 获取暂存的 C 文件。"""
    try:
        result = subprocess.run(
            ['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
            capture_output=True, text=True,
            cwd=project_root
        )
        return [f for f in result.stdout.splitlines() if f.endswith(('.c', '.h'))]
    except Exception:
        return []


def check_error_handling(content, filename):
    """Check C function error code return convention. 检查 C 函数错误码约定。"""
    errors = []
    pattern = r'int\s+(\w+)\s*\([^)]*\)\s*\{'

    for match in re.finditer(pattern, content):
        func_name = match.group(1)
        if func_name in ('main',):
            continue

        start = match.end()
        brace = 1
        end = start
        for i in range(start, min(start + 5000, len(content))):
            if content[i] == '{':
                brace += 1
            elif content[i] == '}':
                brace -= 1
                if brace == 0:
                    end = i
                    break

        body = content[start:end]
        has_error_return = re.search(r'return\s+-[1-9]', body) or re.search(r'return\s+ERROR_', body)

        if not has_error_return:
            errors.append(
                f"  ❌ Function '{func_name}' missing error code return (should use -1/-2/-3 or ERROR_*). "
                f"函数 '{func_name}' 缺少错误码返回（应使用 -1/-2/-3 或 ERROR_*）。"
            )

    return errors


def check_memory_management(content, filename):
    """Check malloc/calloc with corresponding free. 检查 malloc/calloc 是否有 free。"""
    errors = []
    if re.search(r'(malloc|calloc)\s*\(', content):
        if not re.search(r'free\s*\(', content):
            errors.append(
                "  ⚠️  Memory allocation found but no free detected (possible leak or RAII used). "
                "发现内存分配但未找到 free（可能泄漏或使用了 RAII 容器）。"
            )
    return errors


def check_include_order(content, filename):
    """Check include order. 检查 include 顺序。"""
    errors = []
    lines = content.split('\n')
    includes = [(i, line.strip()) for i, line in enumerate(lines) if line.strip().startswith('#include')]

    std_headers = [
        '<stdio.h>', '<stdlib.h>', '<string.h>', '<stdint.h>',
        '<stdbool.h>', '<stddef.h>', '<limits.h>', '<math.h>',
        '<time.h>', '<errno.h>', '<assert.h>', '<ctype.h>'
    ]

    for idx, (i, line) in enumerate(includes):
        if any(h in line for h in std_headers):
            for j in range(idx + 1, len(includes)):
                if '"' in includes[j][1]:
                    errors.append(
                        f"  ❌ Line {includes[j][0]+1}: Project header should be placed after standard library headers. "
                        f"第 {includes[j][0]+1} 行：项目头文件应放在标准库头文件之后。"
                    )
                    break
    return errors


def check_naming(content, filename):
    """Check variable naming: snake_case. 检查变量命名：snake_case。"""
    errors = []
    pattern = r'(int|uint8_t|uint16_t|uint32_t|uint64_t|size_t|char|void\*|float|double)\s+([a-zA-Z_]\w*)\s*[;=]'

    for match in re.finditer(pattern, content):
        var = match.group(2)
        if var[0].isupper() or '_' not in var:
            continue
        if not re.match(r'^[a-z][a-z0-9_]*$', var) and not re.match(r'^k[A-Z]', var):
            errors.append(
                f"  ❌ Variable '{var}' should use snake_case. "
                f"变量 '{var}' 应使用 snake_case。"
            )
    return errors


def check_header_guard(content, filename):
    """Check header file has include guard. 检查头文件是否有 include guard。"""
    errors = []
    if filename.endswith('.h'):
        has_ifndef = re.search(r'#ifndef\s+\w+', content)
        has_define = re.search(r'#define\s+\w+', content)
        if not has_ifndef or not has_define:
            errors.append(
                "  ❌ Header file missing include guard (#ifndef / #define). "
                "头文件缺少 include guard（#ifndef / #define）。"
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
        print("✅ No staged C files. 没有暂存的 C 文件。")
        return 0

    has_error = False

    for fname in files:
        full_path = os.path.join(project_root, fname)
        try:
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception:
            continue

        errors = []
        errors.extend(check_error_handling(content, fname))
        errors.extend(check_memory_management(content, fname))
        errors.extend(check_include_order(content, fname))
        errors.extend(check_naming(content, fname))
        errors.extend(check_header_guard(content, fname))

        if errors:
            has_error = True
            print(f"\n📄 {fname}:")
            for err in errors:
                print(err)

    if has_error:
        print("\n❌ C11 style check failed. Please fix issues and retry. "
              "C11 规范检查未通过，请修复后重新提交。")
        sys.exit(1)
    else:
        print("✅ C11 style check passed. C11 规范检查通过。")
        sys.exit(0)


if __name__ == '__main__':
    main()
#!/usr/bin/env python3
"""
C++20 code style checker. C++20 代码规范检查器。
Checks: naming, forbidden items (NULL, C-style casts, new/delete, raw enum).
检查项：命名规范、禁止项（NULL、C风格转换、new/delete、裸 enum）。
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
    """Get staged C++ files. 获取暂存的 C++ 文件。"""
    try:
        result = subprocess.run(
            ['git', 'diff', '--cached', '--name-only', '--diff-filter=ACM'],
            capture_output=True, text=True,
            cwd=project_root
        )
        return [f for f in result.stdout.splitlines() if f.endswith(('.cpp', '.hpp', '.cc', '.hh'))]
    except Exception:
        return []


def check_naming(content, filename):
    """Check C++ naming conventions. 检查 C++ 命名规范。"""
    errors = []

    # Variables: snake_case
    var_pattern = r'(int|uint8_t|uint16_t|uint32_t|uint64_t|size_t|char|float|double|std::\w+)\s+([a-zA-Z_]\w*)\s*[;=]'
    for m in re.finditer(var_pattern, content):
        var = m.group(2)
        if var[0].isupper() or '_' not in var:
            continue
        if not re.match(r'^[a-z][a-z0-9_]*$', var) and not re.match(r'^k[A-Z]', var):
            errors.append(
                f"  ❌ Variable '{var}' should use snake_case. "
                f"变量 '{var}' 应使用 snake_case。"
            )

    # Class names: PascalCase
    class_pattern = r'class\s+([a-zA-Z_]\w*)'
    for m in re.finditer(class_pattern, content):
        cls = m.group(1)
        if not re.match(r'^[A-Z][a-zA-Z0-9]*$', cls):
            errors.append(
                f"  ❌ Class name '{cls}' should use PascalCase. "
                f"类名 '{cls}' 应使用 PascalCase。"
            )

    # Function names: PascalCase
    func_pattern = r'(?<!class\s+)(?<!struct\s+)(?<!\*)(?<!\&)\b([a-zA-Z_]\w*)\s*\([^)]*\)\s*\{'
    for m in re.finditer(func_pattern, content):
        func = m.group(1)
        if func.startswith('~'):
            continue
        if not re.match(r'^[A-Z][a-zA-Z0-9]*$', func):
            errors.append(
                f"  ❌ Function '{func}' should use PascalCase. "
                f"函数 '{func}' 应使用 PascalCase。"
            )

    # Constants: kPascalCase
    const_pattern = r'const\s+\w+\s+([a-zA-Z_]\w*)\s*[=;]'
    for m in re.finditer(const_pattern, content):
        const = m.group(1)
        if not re.match(r'^k[A-Z][a-zA-Z0-9]*$', const):
            errors.append(
                f"  ❌ Constant '{const}' should use kPascalCase. "
                f"常量 '{const}' 应使用 kPascalCase。"
            )

    # Private members: trailing underscore
    sections = re.split(r'\b(private|protected|public):', content)
    in_private = False
    for sec in sections:
        if sec in ('private', 'protected'):
            in_private = True
            continue
        if in_private and sec == 'public':
            in_private = False
            continue
        if in_private:
            var_decl = re.finditer(r'\b(\w+)\s*[;=]', sec)
            for m in var_decl:
                var = m.group(1)
                if not var.endswith('_'):
                    errors.append(
                        f"  ❌ Private member '{var}' should end with '_'. "
                        f"私有成员 '{var}' 应以 '_' 结尾。"
                    )

    return errors


def check_forbidden(content, filename):
    """Check forbidden items in C++. 检查 C++ 禁止项。"""
    errors = []

    if re.search(r'\bNULL\b', content):
        errors.append(
            "  ❌ 'NULL' is forbidden, use 'nullptr' instead. "
            "禁止使用 'NULL'，应使用 'nullptr'。"
        )

    if re.search(r'\(int\*\)|\(char\*\)|\(void\*\)|\(const\s+\w+\s*\*\)', content):
        errors.append(
            "  ⚠️  Possible C-style cast detected, use static_cast/dynamic_cast/const_cast instead. "
            "发现可能的 C 风格转换，应使用 static_cast/dynamic_cast/const_cast。"
        )

    if re.search(r'\bnew\s+', content) and not re.search(r'unique_ptr|shared_ptr|make_unique|make_shared', content):
        errors.append(
            "  ⚠️  Manual 'new' detected, consider using std::make_unique or std::make_shared. "
            "发现手动 'new'，建议使用 std::make_unique 或 std::make_shared。"
        )

    if re.search(r'\bdelete\s+', content):
        errors.append(
            "  ⚠️  Manual 'delete' detected, use smart pointers instead. "
            "发现手动 'delete'，应使用智能指针。"
        )

    if re.search(r'\benum\s+[a-zA-Z_]\w*\s*\{', content) and not re.search(r'enum\s+class', content):
        errors.append(
            "  ❌ Use 'enum class' instead of raw 'enum'. "
            "应使用 'enum class' 代替裸 'enum'。"
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
        print("✅ No staged C++ files. 没有暂存的 C++ 文件。")
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
        errors.extend(check_naming(content, fname))
        errors.extend(check_forbidden(content, fname))

        if errors:
            has_error = True
            print(f"\n📄 {fname}:")
            for err in errors:
                print(err)

    if has_error:
        print("\n❌ C++20 style check failed. Please fix issues and retry. "
              "C++20 规范检查未通过，请修复后重新提交。")
        sys.exit(1)
    else:
        print("✅ C++20 style check passed. C++20 规范检查通过。")
        sys.exit(0)


if __name__ == '__main__':
    main()
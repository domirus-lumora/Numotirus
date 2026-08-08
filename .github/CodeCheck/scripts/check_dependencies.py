#!/usr/bin/env python3
"""
Dependency scanner. 第三方依赖扫描器。
Scans #include, Git submodules, CMake FetchContent, vcpkg, Conan dependencies.
扫描 #include、Git submodule、CMake FetchContent、vcpkg、Conan 依赖。
Outputs a report for manual review.
输出报告供手动确认。
"""

import os
import re
import sys
import json
import subprocess
from pathlib import Path

# ============================================================
# CONFIGURATION. 配置。
# ============================================================

# C/C++ standard library headers (系统自带，不需要声明)
# 这些是 C/C++ 标准库头文件，不需要在 NOTICE 中声明
STD_HEADERS = {
    # C standard library. C 标准库。
    "stdio.h", "stdlib.h", "string.h", "stdint.h", "stdbool.h", "stddef.h",
    "limits.h", "math.h", "time.h", "errno.h", "assert.h", "ctype.h",
    "wchar.h", "wctype.h", "signal.h", "setjmp.h", "locale.h", "float.h",
    "stdarg.h", "stdatomic.h", "threads.h", "uchar.h", "stdnoreturn.h",
    "complex.h", "fenv.h", "inttypes.h", "iso646.h", "stdalign.h",
    "tgmath.h", "wctype.h",

    # C++ standard library. C++ 标准库。
    "iostream", "fstream", "sstream", "iomanip", "ios", "iosfwd",
    "vector", "array", "list", "deque", "forward_list",
    "map", "set", "unordered_map", "unordered_set",
    "stack", "queue", "priority_queue", "bitset",
    "string", "string_view", "span",
    "algorithm", "numeric", "iterator", "memory", "utility",
    "tuple", "pair", "optional", "variant", "any",
    "functional", "bind", "placeholders",
    "thread", "mutex", "condition_variable", "future", "atomic",
    "chrono", "ratio", "ctime", "duration",
    "regex", "random", "complex", "valarray",
    "type_traits", "concepts", "initializer_list",
    "exception", "stdexcept", "new", "typeinfo", "typeindex",
    "cstdio", "cstdlib", "cstring", "cstdint", "cstdbool", "cstddef",
    "climits", "cmath", "ctime", "cerrno", "cassert", "cctype",
    "cwchar", "cwctype", "csignal", "csetjmp", "clocale", "cfloat",
    "cstdarg", "cstdatomic", "cthreads", "cuchar", "cfenv",
    "cinttypes", "cstdalign", "cstdbool", "cuchar",
    "filesystem", "system_error", "error_code", "error_condition",
    "memory_resource", "polymorphic_allocator", "monotonic_buffer_resource",
    "execution", "barrier", "latch", "semaphore",
    "source_location", "format", "print", "stacktrace",
    "coroutine", "generator", "task",
    "mdspan", "mdarray",
    "ranges", "algorithm", "iterator",
    "syncstream", "osyncstream",
    "csetjmp", "csignal", "cstdalign", "cstdbool", "cuchar",

    # POSIX headers (系统提供). POSIX 头文件。
    "unistd.h", "fcntl.h", "sys/stat.h", "sys/types.h", "sys/socket.h",
    "netinet/in.h", "netdb.h", "arpa/inet.h", "poll.h", "signal.h",
    "pthread.h", "semaphore.h", "dlfcn.h", "dirent.h", "grp.h", "pwd.h",
    "sys/ioctl.h", "sys/select.h", "sys/mman.h", "sys/time.h",
    "sys/times.h", "sys/wait.h", "termios.h", "utime.h",

    # Windows headers (系统提供). Windows 头文件。
    "windows.h", "winsock2.h", "ws2tcpip.h", "wininet.h", "winbase.h",
    "wingdi.h", "winuser.h", "winnls.h", "wincon.h", "winerror.h",
    "winnt.h", "winreg.h", "winsock.h", "shlwapi.h", "shellapi.h",
    "commctrl.h", "commdlg.h", "richedit.h", "dshow.h", "d3d9.h",

    # macOS headers (系统提供). macOS 头文件。
    "CoreFoundation/CoreFoundation.h", "CoreServices/CoreServices.h",
    "Security/Security.h", "SystemConfiguration/SystemConfiguration.h",
    "Foundation/Foundation.h", "AppKit/AppKit.h", "Cocoa/Cocoa.h",
}


# ============================================================
# HELPER FUNCTIONS. 辅助函数。
# ============================================================

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

    # Fallback: traverse up until we find .git
    # 回退方案：向上遍历直到找到 .git
    current = os.path.dirname(os.path.abspath(__file__))
    for _ in range(10):
        if os.path.exists(os.path.join(current, '.git')):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent

    return os.getcwd()


def get_all_source_files(project_root):
    """Get all C/C++ source files in the project. 获取项目中所有 C/C++ 源文件。"""
    source_files = []
    exclude_dirs = {'.git', 'build', 'cmake-build-debug', 'cmake-build-release',
                    'node_modules', 'venv', '.venv', '__pycache__',
                    'third_party', 'third-party', 'deps', '.github', 'out'}

    for root, dirs, files in os.walk(project_root):
        dirs[:] = [d for d in dirs if d not in exclude_dirs]
        for file in files:
            if file.endswith(('.c', '.cpp', '.h', '.hpp', '.cc', '.hh')):
                rel_path = os.path.relpath(os.path.join(root, file), project_root)
                source_files.append(rel_path)

    return source_files


def parse_includes(content):
    """
    Parse all #include directives from content.
    从内容中解析所有 #include 指令。
    """
    includes = []
    lines = content.split('\n')
    for line in lines:
        line = line.strip()
        if line.startswith('#include'):
            match = re.search(r'#include\s+[<"]([^>"]+)[>"]', line)
            if match:
                includes.append(match.group(1))
    return includes


def get_git_submodules(project_root):
    """Get all Git submodules. 获取所有 Git submodule。"""
    try:
        result = subprocess.run(
            ['git', 'submodule', 'status', '--recursive'],
            capture_output=True, text=True,
            cwd=project_root
        )
        submodules = []
        for line in result.stdout.splitlines():
            parts = line.strip().split()
            if len(parts) >= 2:
                submodules.append(parts[1])
        return submodules
    except Exception:
        return []


def get_cmake_fetchcontent(project_root):
    """Parse CMakeLists.txt for FetchContent declarations."""
    """解析 CMakeLists.txt 中的 FetchContent 声明。"""
    deps = []
    for root, dirs, files in os.walk(project_root):
        if '.git' in root or 'build' in root:
            continue
        for file in files:
            if file == 'CMakeLists.txt':
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                    matches = re.finditer(
                        r'FetchContent_Declare\s*\(\s*(\w+)',
                        content,
                        re.IGNORECASE
                    )
                    for m in matches:
                        deps.append(m.group(1))
                except Exception:
                    continue
    return deps


def get_cmake_find_package(project_root):
    """Parse CMakeLists.txt for find_package declarations."""
    """解析 CMakeLists.txt 中的 find_package 声明。"""
    deps = []
    for root, dirs, files in os.walk(project_root):
        if '.git' in root or 'build' in root:
            continue
        for file in files:
            if file == 'CMakeLists.txt':
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                    matches = re.finditer(
                        r'find_package\s*\(\s*(\w+)',
                        content,
                        re.IGNORECASE
                    )
                    for m in matches:
                        deps.append(m.group(1))
                except Exception:
                    continue
    return deps


def get_vcpkg_dependencies(project_root):
    """Parse vcpkg.json for dependencies."""
    """解析 vcpkg.json 中的依赖。"""
    vcpkg_path = os.path.join(project_root, 'vcpkg.json')
    if not os.path.exists(vcpkg_path):
        return []

    try:
        with open(vcpkg_path, 'r', encoding='utf-8', errors='ignore') as f:
            data = json.load(f)
            deps = data.get('dependencies', [])
            result = []
            for dep in deps:
                if isinstance(dep, str):
                    result.append(dep)
                elif isinstance(dep, dict):
                    result.append(dep.get('name', ''))
            return [d for d in result if d]
    except Exception:
        return []


def get_conan_dependencies(project_root):
    """Parse conanfile.txt or conanfile.py for dependencies."""
    """解析 conanfile.txt 或 conanfile.py 中的依赖。"""
    deps = []
    conan_txt = os.path.join(project_root, 'conanfile.txt')
    conan_py = os.path.join(project_root, 'conanfile.py')

    if os.path.exists(conan_txt):
        try:
            with open(conan_txt, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            in_requires = False
            for line in content.splitlines():
                line = line.strip()
                if line == '[requires]':
                    in_requires = True
                    continue
                if line.startswith('['):
                    in_requires = False
                if in_requires and line and not line.startswith('#'):
                    dep = line.split('/')[0] if '/' in line else line
                    deps.append(dep)
        except Exception:
            pass

    if os.path.exists(conan_py):
        try:
            with open(conan_py, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            matches = re.finditer(r'requires\s*=\s*\(\s*"([^"]+)"', content)
            for m in matches:
                dep = m.group(1).split('/')[0] if '/' in m.group(1) else m.group(1)
                deps.append(dep)
        except Exception:
            pass

    return deps


# ============================================================
# MAIN SCAN FUNCTION. 主扫描函数。
# ============================================================

def main():
    """Main entry point. 主入口。"""
    PROJECT_ROOT = get_project_root()

    # Change to project root
    # 切换到项目根目录
    try:
        os.chdir(PROJECT_ROOT)
    except Exception:
        pass

    print()
    print("=" * 70)
    print("🔍 THIRD-PARTY DEPENDENCY SCANNER")
    print("🔍 第三方依赖扫描器")
    print("=" * 70)
    print()
    print(f"📂 Project root: {PROJECT_ROOT}")
    print(f"📂 项目根目录: {PROJECT_ROOT}")
    print()

    # 1. Scan #include directives. 扫描 #include 指令。
    print("📄 [1/6] Scanning #include directives...")
    print("📄 [1/6] 正在扫描 #include 指令...")

    all_includes = set()
    source_files = get_all_source_files(PROJECT_ROOT)
    print(f"  Found {len(source_files)} source files.")
    print(f"  找到 {len(source_files)} 个源文件。")

    for fname in source_files:
        full_path = os.path.join(PROJECT_ROOT, fname)
        try:
            with open(full_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                includes = parse_includes(content)
                all_includes.update(includes)
        except Exception as e:
            print(f"  ⚠️  Skipping {fname}: {e}")

    print(f"  ✅ Found {len(all_includes)} unique #includes.")
    print(f"  ✅ 找到 {len(all_includes)} 个不同的 #include。")
    print()

    # 2. Scan Git submodules. 扫描 Git submodule。
    print("📦 [2/6] Scanning Git submodules...")
    print("📦 [2/6] 正在扫描 Git submodule...")
    submodules = get_git_submodules(PROJECT_ROOT)
    if submodules:
        print(f"  ✅ Found {len(submodules)} submodules:")
        print(f"  ✅ 找到 {len(submodules)} 个 submodule:")
        for sm in submodules:
            print(f"      - {sm}")
    else:
        print("  ℹ️  No submodules found. 未找到 submodule。")
    print()

    # 3. Scan CMake FetchContent. 扫描 CMake FetchContent。
    print("📦 [3/6] Scanning CMake FetchContent...")
    print("📦 [3/6] 正在扫描 CMake FetchContent...")
    fetchcontent = get_cmake_fetchcontent(PROJECT_ROOT)
    if fetchcontent:
        print(f"  ✅ Found {len(fetchcontent)} FetchContent packages:")
        print(f"  ✅ 找到 {len(fetchcontent)} 个 FetchContent 包:")
        for dep in fetchcontent:
            print(f"      - {dep}")
    else:
        print("  ℹ️  No FetchContent found. 未找到 FetchContent。")
    print()

    # 4. Scan CMake find_package. 扫描 CMake find_package。
    print("📦 [4/6] Scanning CMake find_package...")
    print("📦 [4/6] 正在扫描 CMake find_package...")
    findpkg = get_cmake_find_package(PROJECT_ROOT)
    if findpkg:
        print(f"  ✅ Found {len(findpkg)} find_package calls:")
        print(f"  ✅ 找到 {len(findpkg)} 个 find_package 调用:")
        for dep in findpkg:
            print(f"      - {dep}")
    else:
        print("  ℹ️  No find_package found. 未找到 find_package。")
    print()

    # 5. Scan vcpkg. 扫描 vcpkg。
    print("📦 [5/6] Scanning vcpkg.json...")
    print("📦 [5/6] 正在扫描 vcpkg.json...")
    vcpkg_deps = get_vcpkg_dependencies(PROJECT_ROOT)
    if vcpkg_deps:
        print(f"  ✅ Found {len(vcpkg_deps)} vcpkg dependencies:")
        print(f"  ✅ 找到 {len(vcpkg_deps)} 个 vcpkg 依赖:")
        for dep in vcpkg_deps:
            print(f"      - {dep}")
    else:
        print("  ℹ️  No vcpkg.json found or no dependencies. 未找到 vcpkg.json 或无依赖。")
    print()

    # 6. Scan Conan. 扫描 Conan。
    print("📦 [6/6] Scanning Conan...")
    print("📦 [6/6] 正在扫描 Conan...")
    conan_deps = get_conan_dependencies(PROJECT_ROOT)
    if conan_deps:
        print(f"  ✅ Found {len(conan_deps)} Conan dependencies:")
        print(f"  ✅ 找到 {len(conan_deps)} 个 Conan 依赖:")
        for dep in conan_deps:
            print(f"      - {dep}")
    else:
        print("  ℹ️  No Conan dependencies found. 未找到 Conan 依赖。")
    print()

    # 7. Summary. 总结。
    print("=" * 70)
    print("📋 SUMMARY. 总结。")
    print("=" * 70)
    print(f"  #includes (unique):     {len(all_includes)}")
    print(f"  Git submodules:          {len(submodules)}")
    print(f"  CMake FetchContent:      {len(fetchcontent)}")
    print(f"  CMake find_package:      {len(findpkg)}")
    print(f"  vcpkg:                   {len(vcpkg_deps)}")
    print(f"  Conan:                   {len(conan_deps)}")
    print("=" * 70)

    # 8. Show non-standard includes. 显示非标准库的 include。
    print()
    print("📌 NON-STANDARD INCLUDES (may need to be declared in NOTICE)")
    print("📌 非标准库 INCLUDE（可能需要在 NOTICE 中声明）")
    print("-" * 70)

    non_std = [inc for inc in sorted(all_includes) if inc not in STD_HEADERS]

    if non_std:
        print()
        for inc in non_std:
            print(f"    #include <{inc}>")
        print()
        print(f"  Total: {len(non_std)}")
        print(f"  共 {len(non_std)} 个")
    else:
        print("  ✅ All includes are standard library headers.")
        print("  ✅ 所有 include 都是标准库头文件。")

    print()
    print("=" * 70)
    print("💡 Next steps. 下一步。")
    print("=" * 70)
    print("  1. Review the 'Non-standard includes' list above.")
    print("  2. For each third-party library, check its license.")
    print("  3. If it requires attribution, add it to NOTICE.")
    print()
    print("  1. 检查上面的 '非标准库 include' 列表。")
    print("  2. 对每个第三方库，确认其许可证。")
    print("  3. 如果要求声明，添加到 NOTICE 文件中。")
    print("=" * 70)
    print()

    # Always exit 0 (warning only, don't block commit).
    # 始终以 0 退出（仅警告，不阻止提交）。
    sys.exit(0)


if __name__ == '__main__':
    main()
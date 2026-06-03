# CMake minimum version required. 要求的最低 CMake 版本。
cmake_minimum_required(VERSION 3.20)

# Project name for core module. core 模块的项目名称。
project(NumotirusCore VERSION 0.0.1)

# Use C++20 standard. 使用 C++20 标准。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find libsodium using pkg-config. 使用 pkg-config 查找 libsodium。
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED libsodium IMPORTED_TARGET)

# Static library numotirus_core. 静态库 numotirus_core。
add_library(numotirus_core STATIC
    crypto/crypto.cpp
)

# Add include directory. 添加头文件目录。
target_include_directories(numotirus_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libsodium. 链接 libsodium。
target_link_libraries(numotirus_core PRIVATE PkgConfig::LIBSODIUM)

# Build crypto test (optional, enabled by default). 构建 crypto 测试（可选，默认启用）。
option(BUILD_CRYPTO_TEST "Build crypto test" ON)
if(BUILD_CRYPTO_TEST)
    add_executable(crypto_test crypto/crypto_test.cpp)
    target_link_libraries(crypto_test numotirus_core PkgConfig::LIBSODIUM)
    # Add test for ctest. 添加测试供 ctest 使用。
    add_test(NAME CryptoTest COMMAND crypto_test)
endif()

# Build crypto CLI (optional, disabled by default). 构建 crypto 命令行工具（可选，默认关闭）。
option(BUILD_CRYPTO_CLI "Build crypto CLI tool" OFF)
if(BUILD_CRYPTO_CLI)
    add_executable(crypto_cli crypto/crypto_cli.cpp)
    target_link_libraries(crypto_cli numotirus_core PkgConfig::LIBSODIUM)
endif()

# Export library for use by root CMake. 导出库供根 CMake 使用。
export(TARGETS numotirus_core FILE ${CMAKE_CURRENT_BINARY_DIR}/numotirus_core-targets.cmake)
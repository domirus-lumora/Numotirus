# CMake minimum version required. 要求的最低 CMake 版本。
cmake_minimum_required(VERSION 3.20)

# Project name and version. 项目名称和版本。
project(Numotirus VERSION 0.0.1)

# Use C++20 standard. 使用 C++20 标准。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add core subdirectory. 添加 core 子目录。
add_subdirectory(core)

# Optionally build CLI tool (disabled by default). 可选构建 CLI 工具（默认关闭）。
option(BUILD_CLI "Build CLI tool" OFF)
if(BUILD_CLI)
    add_subdirectory(cli)
endif()
# SychevS/p2p
add_subdirectory(core/p2p/dht)
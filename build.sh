#!/bin/bash
# Numotirus 通用编译脚本 / Universal build script
# 支持 / Supports: Linux, macOS (Intel + ARM), Android (Termux)
# 用法 / Usage: ./build.sh

set -e

echo "🔧 Numotirus Builder"
echo "====================="

# 检测平台 / Detect platform
OS=$(uname -s)
ARCH=$(uname -m)

echo "📦 OS: $OS"
echo "💻 Arch: $ARCH"

# 检测 Termux / Detect Termux
if [ -d "/data/data/com.termux" ]; then
    IS_TERMUX=true
    echo "📱 Termux detected"
else
    IS_TERMUX=false
fi

# 设置编译器 / Set compiler
if [ "$OS" = "Darwin" ]; then
    CXX="g++"
    CC="gcc"
    if [ "$ARCH" = "arm64" ]; then
        echo "🍎 Apple Silicon (ARM64)"
    else
        echo "🍎 Intel Mac"
    fi
elif [ "$IS_TERMUX" = true ] || [ "$OS" = "Linux" ]; then
    CXX="g++"
    CC="gcc"
    echo "🐧 Linux"
else
    echo "❌ Unsupported OS: $OS"
    exit 1
fi

# 检查 libsodium / Check libsodium
if ! pkg-config --exists libsodium 2>/dev/null; then
    echo "⚠️  libsodium not found, attempting to install..."
    if [ "$IS_TERMUX" = true ]; then
        pkg install libsodium -y
    elif [ "$OS" = "Darwin" ]; then
        if ! command -v brew &> /dev/null; then
            echo "❌ Homebrew not found. Please install libsodium manually."
            exit 1
        fi
        brew install libsodium
    else
        sudo apt-get update && sudo apt-get install -y libsodium-dev
    fi
fi

echo "✅ Building..."

# 编译所有 .o 文件 / Compile all .o files
$CXX -std=c++20 -c core/p2p/dht.cpp -o dht.o -Icore/p2p
$CXX -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o -Icore/p2p
$CC -c core/p2p/kcp/ikcp.c -o ikcp.o
$CC -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto

# noise-cpp 源文件
$CXX -std=c++20 -c core/protocol/noise-cpp/noise.cpp -Icore/protocol/noise-cpp -o noise_cpp.o
$CC -c core/protocol/noise-cpp/monocypher.c -Icore/protocol/noise-cpp -o monocypher.o
$CC -c core/protocol/noise-cpp/rng_get_bytes.c -Icore/protocol/noise-cpp -o rng_get_bytes.o

# Noise protocol. Noise 协议封装
$CXX -std=c++20 -c core/protocol/noise.cpp -o noise.o -Icore/protocol -Icore/protocol/noise-cpp -Icore

# P2P. P2P 模块
$CXX -std=c++20 -c core/p2p/p2p_core.cpp -o p2p_core.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
$CXX -std=c++20 -c core/p2p/p2p_session.cpp -o p2p_session.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
$CXX -std=c++20 -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore

# NAT 模块 / NAT modules
$CXX -std=c++20 -c core/p2p/nat/nat_stun.cpp -o nat_stun.o -Icore/p2p
$CXX -std=c++20 -c core/p2p/nat/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p
$CXX -std=c++20 -c core/p2p/nat/port_prediction.cpp -o port_prediction.o -Icore/p2p
$CXX -std=c++20 -c core/p2p/nat/nat_traversal.cpp -o nat_traversal.o -Icore/p2p

# 传输层模块 / Transport module
$CXX -std=c++20 -c core/transport/transport.cpp -o transport.o -Icore/p2p -Icore/crypto -Icore/transport -Icore/protocol/noise-cpp -Icore

# 链接 / Link
$CXX -o p2p_chat *.o -lstdc++ -lsodium -lpthread

echo "✅ Done! Run: ./p2p_chat 8080"
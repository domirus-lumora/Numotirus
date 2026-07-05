#!/bin/bash
# iOS 交叉编译脚本 / iOS cross-compile script
# 需要在 macOS 上运行，需要 Xcode / Requires macOS with Xcode
# 用法 / Usage: ./build_ios.sh

set -e

echo "🍎 Numotirus iOS Builder"
echo "========================"

# 检查 Xcode / Check Xcode
if ! xcode-select -p &>/dev/null; then
    echo "❌ Xcode not found. Please install Xcode."
    exit 1
fi

# iOS SDK 路径 / iOS SDK path
SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path)
if [ -z "$SDK_PATH" ]; then
    echo "❌ iOS SDK not found"
    exit 1
fi

echo "📦 SDK: $SDK_PATH"

# 设置交叉编译 / Set cross-compile flags
export CC="xcrun --sdk iphoneos clang"
export CXX="xcrun --sdk iphoneos clang++"
export CFLAGS="-arch arm64 -isysroot $SDK_PATH -miphoneos-version-min=12.0"
export CXXFLAGS="-arch arm64 -isysroot $SDK_PATH -miphoneos-version-min=12.0"
export LDFLAGS="-arch arm64 -isysroot $SDK_PATH"

echo "🔨 Building for iOS (arm64)..."

# 编译 / Build
$CXX -std=c++20 -c core/p2p/dht.cpp -o dht.o $CXXFLAGS
$CXX -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o $CXXFLAGS
$CC -c core/p2p/kcp/ikcp.c -o ikcp.o $CFLAGS
$CC -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto $CFLAGS
$CC -c core/protocol/zrtp.c -o zrtp.o -Icore/protocol -Icore/crypto $CFLAGS
$CXX -std=c++20 -c core/p2p/p2p.cpp -o p2p.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport $CXXFLAGS
$CXX -std=c++20 -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport $CXXFLAGS

# NAT 模块
$CXX -std=c++20 -c core/p2p/nat_stun.cpp -o nat_stun.o -Icore/p2p $CXXFLAGS
$CXX -std=c++20 -c core/p2p/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p $CXXFLAGS
$CXX -std=c++20 -c core/p2p/port_prediction.cpp -o port_prediction.o -Icore/p2p $CXXFLAGS
$CXX -std=c++20 -c core/p2p/nat_traversal.cpp -o nat_traversal.o -Icore/p2p $CXXFLAGS

# 传输层模块
$CXX -std=c++20 -c core/transport/transport.cpp -o transport.o -Icore/p2p -Icore/crypto -Icore/transport $CXXFLAGS

# 打包成静态库 / Package as static library
ar rcs libnumotirus.a *.o

echo "✅ iOS library built: libnumotirus.a"
echo "📝 To use in Xcode: link libnumotirus.a and add -lsodium"
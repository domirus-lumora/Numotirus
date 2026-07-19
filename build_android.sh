#!/bin/bash
# Android Termux 专用编译脚本 / Android Termux build script
# 用法 / Usage: ./build_android.sh

set -e

echo "📱 Numotirus Android Builder"
echo "============================="

# 安装依赖 / Install dependencies
pkg update -y
pkg install -y clang make libsodium

# 编译 / Build
clang++ -std=c++20 -c core/p2p/dht.cpp -o dht.o -Icore/p2p
clang++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o -Icore/p2p
clang -c core/p2p/kcp/ikcp.c -o ikcp.o
clang -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto

# noise-cpp 源文件
clang++ -std=c++20 -c core/protocol/noise-cpp/noise.cpp -Icore/protocol/noise-cpp -o noise_cpp.o
clang -c core/protocol/noise-cpp/monocypher.c -Icore/protocol/noise-cpp -o monocypher.o
clang -c core/protocol/noise-cpp/rng_get_bytes.c -Icore/protocol/noise-cpp -o rng_get_bytes.o

# Noise protocol. Noise 协议封装
clang++ -std=c++20 -c core/protocol/noise.cpp -o noise.o -Icore/protocol -Icore/protocol/noise-cpp -Icore

# P2P. P2P 模块
clang++ -std=c++20 -c core/p2p/p2p_core.cpp -o p2p_core.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
clang++ -std=c++20 -c core/p2p/p2p_session.cpp -o p2p_session.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
clang++ -std=c++20 -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore

# NAT 模块
clang++ -std=c++20 -c core/p2p/nat/nat_stun.cpp -o nat_stun.o -Icore/p2p
clang++ -std=c++20 -c core/p2p/nat/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p
clang++ -std=c++20 -c core/p2p/nat/port_prediction.cpp -o port_prediction.o -Icore/p2p
clang++ -std=c++20 -c core/p2p/nat/nat_traversal.cpp -o nat_traversal.o -Icore/p2p

# 传输层模块
clang++ -std=c++20 -c core/transport/transport.cpp -o transport.o -Icore/p2p -Icore/crypto -Icore/transport -Icore/protocol/noise-cpp -Icore

# 链接 / Link
clang++ -o p2p_chat *.o -lstdc++ -lsodium -lpthread

echo "✅ Done! Run: ./p2p_chat 8080"
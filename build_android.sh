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
clang++ -std=c++20 -c core/p2p/dht.cpp -o dht.o
clang++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o
clang -c core/p2p/kcp/ikcp.c -o ikcp.o
clang -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto
clang -c core/protocol/zrtp.c -o zrtp.o -Icore/protocol -Icore/crypto
clang++ -c core/p2p/p2p.cpp -o p2p.o -Icore/p2p -Icore/crypto -Icore/protocol
clang++ -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol

# NAT 模块
clang++ -c core/p2p/nat_stun.cpp -o nat_stun.o -Icore/p2p
clang++ -c core/p2p/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p
clang++ -c core/p2p/port_prediction.cpp -o port_prediction.o -Icore/p2p
clang++ -c core/p2p/nat_traversal.cpp -o nat_traversal.o -Icore/p2p

# 链接 / Link
clang++ -o p2p_chat p2p_chat.o p2p.o dht.o dht_c.o nat_stun.o udp_hole_punch.o port_prediction.o nat_traversal.o zrtp.o crypto_c.o ikcp.o -lstdc++ -lsodium -lpthread

echo "✅ Done! Run: ./p2p_chat 8080"
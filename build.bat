@echo off
echo [Numotirus Builder]
echo ===================

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found.
    exit /b 1
)

echo [OK] Compiling...

:: Important part. 核心模块
g++ -std=c++20 -c core/p2p/dht.cpp -o dht.o -Icore/p2p
g++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o -Icore/p2p
gcc -c core/p2p/kcp/ikcp.c -o ikcp.o
gcc -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto

:: noise-cpp 源文件
g++ -std=c++20 -c core/protocol/noise-cpp/noise.cpp -Icore/protocol/noise-cpp -o noise_cpp.o
gcc -c core/protocol/noise-cpp/monocypher.c -Icore/protocol/noise-cpp -o monocypher.o
gcc -c core/protocol/noise-cpp/rng_get_bytes.c -Icore/protocol/noise-cpp -o rng_get_bytes.o

:: Noise protocol. Noise 协议封装
g++ -std=c++20 -c core/protocol/noise.cpp -o noise.o -Icore/protocol -Icore/protocol/noise-cpp -Icore

:: P2P. P2P 模块
g++ -std=c++20 -c core/p2p/p2p_core.cpp -o p2p_core.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
g++ -std=c++20 -c core/p2p/p2p_session.cpp -o p2p_session.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore
g++ -std=c++20 -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol -Icore/transport -Icore/protocol/noise-cpp -Icore

:: NAT. NAT 模块
g++ -std=c++20 -c core/p2p/nat/nat_stun.cpp -o nat_stun.o -Icore/p2p
g++ -std=c++20 -c core/p2p/nat/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p
g++ -std=c++20 -c core/p2p/nat/port_prediction.cpp -o port_prediction.o -Icore/p2p
g++ -std=c++20 -c core/p2p/nat/nat_traversal.cpp -o nat_traversal.o -Icore/p2p

:: Transport. 传输层
g++ -std=c++20 -c core/transport/transport.cpp -o transport.o -Icore/p2p -Icore/crypto -Icore/transport -Icore/protocol/noise-cpp -Icore

:: Link. 链接
g++ -fcommon -o p2p_chat.exe *.o -lstdc++ -lsodium -lws2_32 -lpthread

if errorlevel 1 (
    echo [ERROR] Link failed!
    exit /b 1
)

echo [OK] Done! Run: p2p_chat.exe 8080
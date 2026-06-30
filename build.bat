@echo off
:: Numotirus Windows 编译脚本 / Windows build script
:: 用法 / Usage: build.bat

echo [Numotirus Builder]
echo ===================

:: 检查 g++ / Check g++
where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found. Please install MinGW or MSYS2.
    exit /b 1
)

echo [OK] Compiling...

g++ -std=c++20 -c core/p2p/dht.cpp -o dht.o
g++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o
gcc -c core/p2p/kcp/ikcp.c -o ikcp.o
gcc -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto
gcc -c core/protocol/zrtp.c -o zrtp.o -Icore/protocol -Icore/crypto
gcc -c core/p2p/p2p.c -o p2p.o -Icore/p2p -Icore/crypto -Icore/protocol
g++ -c core/p2p/p2p_chat.cpp -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol

:: NAT
g++ -c core/p2p/nat_stun.cpp -o nat_stun.o -Icore/p2p
g++ -c core/p2p/udp_hole_punch.cpp -o udp_hole_punch.o -Icore/p2p
g++ -c core/p2p/port_prediction.cpp -o port_prediction.o -Icore/p2p
g++ -c core/p2p/nat_traversal.cpp -o nat_traversal.o -Icore/p2p

:: 链接/link
g++ -o p2p_chat.exe p2p_chat.o p2p.o dht.o dht_c.o nat_stun.o udp_hole_punch.o port_prediction.o nat_traversal.o zrtp.o crypto_c.o ikcp.o -lstdc++ -lsodium -lws2_32 -lpthread

echo [OK] Done! Run: p2p_chat.exe 8080
# Numotirus Noise 协议模块

基于 Noise Protocol Framework 的密钥交换实现，提供 XX 模式握手、SAS 短认证字符串生成和信任存储。

---

## 功能

- Noise XX 模式握手（交互式，双方身份隐藏）
- X25519 椭圆曲线密钥交换
- ChaCha20-Poly1305 对称加密
- BLAKE2s 哈希函数
- SAS（Short Authentication String）生成（4 组 4 位数字）
- SAS 手动验证与状态标记
- 信任存储（公钥 + 共享秘密持久化）
- 无角色依赖（基于 Noise 协议标准）

---

## 文件结构

```text
core/protocol/
├── noise.hpp          # 头文件，公开 API（封装层）
├── noise.cpp          # 实现（封装层）
└── noise-cpp/         # Noise 协议实现（submodule，原作者仓库）
    ├── noise.h
    ├── noise.cpp
    ├── ...
```

---

## 编译

```bash
cd core/protocol
g++ -std=c++20 -c noise.cpp -I. -Inoise-cpp -o noise.o
g++ -std=c++20 -c noise-cpp/noise.cpp -Inoise-cpp -o noise_cpp.o
gcc -c noise-cpp/monocypher.c -Inoise-cpp -o monocypher.o
gcc -c noise-cpp/rng_get_bytes.c -Inoise-cpp -o rng_get_bytes.o
```

---

## 使用

```cpp
#include "core/protocol/noise.hpp"

using namespace numotirus::protocol::noise;

// 1. 生成密钥对
auto kp = GenerateKeyPair();

// 2. 创建会话
NoiseSession session;
session.SetKeyPair(kp.value());
session.SetPeerPublic(peer_public_key);

// 3. 执行握手
auto err = session.Handshake(
    true,  // 发起方
    [](const uint8_t* data, size_t len) -> ErrorCode {
        // 发送数据到对方
        return ErrorCode::kSuccess;
    },
    [](uint8_t* buffer, size_t len) -> ErrorCode {
        // 从对方接收数据
        return ErrorCode::kSuccess;
    }
);

if (err != ErrorCode::kSuccess) {
    // 处理错误
}

// 4. 获取 SAS 码，让用户口头比对
std::string sas = session.GetSas();
std::cout << "SAS: " << sas << std::endl;

// 5. 用户确认后标记已验证
session.MarkVerified();

// 6. 获取会话密钥
auto rx_key = session.GetRxKey();
auto tx_key = session.GetTxKey();

// 7. 检查握手是否完成
if (session.IsHandshakeComplete()) {
    // 可以开始加密通信
}
```

---

## 信任存储

```cpp
// 保存信任（首次验证后调用）
TrustSave("peer_id", peer_public_key, shared_secret);

// 加载信任（下次连接时检查）
std::array<uint8_t, kPublicKeySize> pubkey;
std::array<uint8_t, kSharedKeySize> secret;
if (TrustLoad("peer_id", pubkey, secret) == ErrorCode::kSuccess) {
    // 信任存在，跳过 SAS 比对
}
```

---

## API 参考

| 函数 | 说明 |
| ------ | ------ |
| `GenerateKeyPair()` | 生成 X25519 密钥对 |
| `NoiseSession()` | 创建会话 |
| `SetKeyPair()` | 设置本地密钥对 |
| `SetPeerPublic()` | 设置对方公钥 |
| `Handshake()` | 执行 Noise XX 握手 |
| `GetSas()` | 获取 SAS 字符串 |
| `MarkVerified()` | 标记已验证 |
| `IsVerified()` | 检查是否已验证 |
| `GetRxKey()` | 获取接收密钥 |
| `GetTxKey()` | 获取发送密钥 |
| `IsHandshakeComplete()` | 检查握手是否完成 |
| `TrustSave()` | 保存信任 |
| `TrustLoad()` | 加载信任 |

---

## 测试

```bash
# 编译并运行测试
g++ -std=c++20 -o test_noise test_noise.cpp noise.cpp noise-cpp/noise.cpp monocypher.c rng_get_bytes.c -I. -Inoise-cpp -lsodium
./test_noise
```

预期输出：

```bash
=== Noise Module Test ===

✅ test_keypair_generation: PASSED
✅ test_handshake: PASSED
   SAS: 1234 5678 9012 3456
✅ test_verified_flag: PASSED
   Verified: 0 -> 1
✅ test_trust_store: PASSED

=== Summary ===
Passed: 4/4
```

---

## 依赖

- [libsodium](https://doc.libsodium.org/) 1.0.18+
- [noise-cpp](https://github.com/ethindp/noise-cpp)（submodule）

---

## 协议说明

- **握手模式**: Noise XX（交互式，双方身份隐藏）
- **椭圆曲线**: X25519
- **对称加密**: ChaCha20-Poly1305
- **哈希函数**: BLAKE2s
- **SAS 生成**: 4 组 4 位数字（从握手哈希派生）

---

## 作者

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

---

## 许可证

Apache 2.0

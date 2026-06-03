# Numotirus Crypto Module

C++20 加密模块，提供 X25519 密钥交换、XChaCha20-Poly1305 认证加密、ECIES 公钥加密。

## 功能

- 密钥对生成（X25519）
- 公钥加密（ECIES 风格）
- 私钥解密
- 对称加密（XChaCha20-Poly1305）
- 密钥派生（BLAKE2b）
- 随机字节生成

## 依赖

- [libsodium](https://doc.libsodium.org/) 1.0.18+

## 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## 测试

```bash
ctest
```

## 使用示例

### 公钥加密

```cpp
#include "crypto.hpp"

auto keypair = numotirus::crypto::generate_keypair();
// 把 keypair.public_key 给对方

auto cipher = numotirus::crypto::encrypt_public(plaintext, recipient_public);
auto plain = numotirus::crypto::decrypt_private(cipher, my_secret);
```

### 对称加密

```cpp
auto key = numotirus::crypto::random_bytes(32);
auto nonce = numotirus::crypto::random_bytes(24);
auto cipher = numotirus::crypto::encrypt(plaintext, key, nonce);
auto plain = numotirus::crypto::decrypt(cipher, key, nonce);
```

## 命令行工具

编译时开启 `-DBUILD_CRYPTO_CLI=ON` 会生成 `crypto_cli.exe`，提供交互式加解密。

## 安全说明

本模块已通过单元测试，但**未经独立密码学审计**。生产环境使用需自行评估风险。

## 许可证

Apache 2.0

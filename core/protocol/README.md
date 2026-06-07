# ZRTP Module for Numotirus

ZRTP 协议实现，基于 X25519 + BLAKE2b，提供密钥交换、SAS 短认证字符串生成和信任存储。

---

## 功能 Features

- X25519 密钥交换
- 短认证字符串（SAS）生成（4 组 4 位数字）
- SAS 手动验证与状态标记
- 信任存储（公钥 + 共享秘密持久化）
- 无角色依赖（基于公钥字典序自动协商 client/server）

---

## 文件结构 Files

```text
core/protocol/
├── zrtp.h          # 头文件，公开 API
├── zrtp.c          # 实现
└── test_zrtp.c     # 单元测试
```

---

## 编译 Build

```bash
cd core/protocol
gcc -c zrtp.c -o zrtp.o -lsodium
```

单元测试：

```bash
gcc -o test_zrtp test_zrtp.c zrtp.c -lsodium
./test_zrtp
```

---

## 使用 Usage

```c
#include "core/protocol/zrtp.h"

// 1. 创建会话
zrtp_session_t* sess = zrtp_session_new();

// 2. 设置自己的密钥对（从 crypto 模块获取）
zrtp_session_set_keypair(sess, my_pubkey, my_seckey);

// 3. 设置对方的公钥（通过 P2P 交换获得）
zrtp_session_set_peer_public(sess, peer_pubkey);

// 4. 执行密钥交换
if (zrtp_session_key_exchange(sess) != ZRTP_SUCCESS) {
    // 处理错误
}

// 5. 获取 SAS 码，让用户口头比对
const char* sas = zrtp_session_get_sas(sess);
printf("SAS: %s\n", sas);

// 6. 用户确认后标记已验证
zrtp_session_mark_verified(sess);

// 7. 获取共享秘密用于后续加密
const uint8_t* secret = zrtp_session_get_shared_secret(sess);

// 8. 清理
zrtp_session_free(sess);
```

---

## 信任存储 Trust Store

```c
// 保存信任（首次验证后调用）
zrtp_trust_store_save("peer_id", peer_pubkey, shared_secret);

// 加载信任（下次连接时检查）
uint8_t saved_pubkey[32], saved_secret[32];
if (zrtp_trust_store_load("peer_id", saved_pubkey, saved_secret) == ZRTP_SUCCESS) {
    // 信任存在，跳过 SAS 比对
}
```

---

## API 参考

| 函数 | 说明 |
| ------ | ------ |
| `zrtp_session_new()` | 创建会话 |
| `zrtp_session_free()` | 销毁会话 |
| `zrtp_session_set_keypair()` | 设置本地密钥对 |
| `zrtp_session_set_peer_public()` | 设置对方公钥 |
| `zrtp_session_key_exchange()` | 执行密钥交换 |
| `zrtp_session_get_sas()` | 获取 SAS 字符串 |
| `zrtp_session_mark_verified()` | 标记已验证 |
| `zrtp_session_is_verified()` | 检查是否已验证 |
| `zrtp_session_get_shared_secret()` | 获取共享秘密 |
| `zrtp_trust_store_save()` | 保存信任 |
| `zrtp_trust_store_load()` | 加载信任 |

---

## 测试 Test

运行单元测试验证 SAS 一致性、不同密钥对差异、验证标志和信任存储：

```bash
gcc -o test_zrtp test_zrtp.c zrtp.c -lsodium
./test_zrtp
```

预期输出：

```bash
=== ZRTP Module Test ===

✅ test_same_sas: PASSED
   SAS: 1234 5678 9012 3456
✅ test_different_sas: PASSED
   SAS1: 1234 5678 9012 3456
   SAS2: 6543 2109 8765 4321
✅ test_verified_flag: PASSED
   Verified: 0 -> 1
✅ test_trust_store: PASSED

=== Summary ===
Passed: 4/4
```

---

## 依赖 Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+

---

## 作者 Author

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

---

## 许可证 License

Apache 2.0

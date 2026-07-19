# Numotirus 架构

本文档面向希望理解或贡献核心代码的开发者。

## 设计原则

1. **模块化**：加密层、网络层、协议层、应用层严格分离。
2. **够用就好**：先跑通核心流程。第一个目标：两人在命令行完成加密消息收发。
3. **开源优先**：核心协议 Apache 2.0。
4. **沉默优先**：默认不广播、不心跳、不留痕迹。

## 分层架构

| 层级 | 目录 | 语言 | 职责 | 状态 |
| ------ | ------ | ------ | ------ | ------ |
| **应用层** | `cli/`, `p2p_chat.c` | C++ / C | 命令行测试工具 | ✅ 可运行 |
| **协议层** | `core/protocol/` | C++ | Noise XX 握手、SAS 验证 | ✅ 已完成 |
| **网络层** | `core/p2p/` | C | UDP + KCP 可靠传输，跨平台 | ✅ 已完成 |
| **加密层** | `core/crypto/` | C | X25519 + XChaCha20-Poly1305 + ECIES | ✅ 已完成 |
| **插件层** | `core/plugin/` | C (ABI) | 动态加载扩展 | ❌ 待实现 |
| **GUI 层** | `gui/` | C# / Avalonia | 跨平台图形界面 | ❌ 待实现 |

## 核心模块详解

### 加密层 (`core/crypto/`)

**状态**：✅ 稳定，提供 `crypto_c.h` C 接口。

**功能**：

- X25519 密钥对生成、密钥交换
- XChaCha20-Poly1305 认证加密
- ECIES 公钥加密（`crypto_encrypt_public` / `crypto_decrypt_private`）
- BLAKE2b 密钥派生

**依赖**：`libsodium`

```c
// 使用示例
CryptoKeypair* kp = crypto_keypair_generate();
crypto_encrypt_public(plain, len, peer_pubkey, &cipher, &clen);
crypto_decrypt_private(cipher, clen, my_seckey, &plain, &plen);
```

---

### 网络层 (`core/p2p/`)

**状态**：✅ 已完成，基于 KCP + select 实现。

**功能**：

- UDP + KCP 可靠传输（重传、有序、拥塞控制）
- `select()` 非阻塞接收，线程安全退出
- 跨平台（Windows/Linux）

```c
// 使用示例
P2PNode* node = p2p_create(8888);
p2p_set_peer_key(node, peer_pubkey);
p2p_start(node);
p2p_send(node, "127.0.0.1", 8888, (uint8_t*)"hello", 5);
```

---

### 协议层 (`core/protocol/`)

**状态**：✅ Noise 协议握手已完成。

**功能**：

- Noise XX 模式握手（交互式，双方身份隐藏）
- X25519 密钥交换
- SAS（短认证字符串）生成与比对
- 信任存储（TOFU）

```cpp
// 使用示例
NoiseSession session;
session.SetKeyPair(kp);
session.SetPeerPublic(peer_pub);
auto err = session.Handshake(true, write_cb, read_cb);
std::string sas = session.GetSas();
```

**依赖**：`noise-cpp`（submodule）

---

### 插件层 (`core/plugin/`) - 生态关键

**接口草案**：

```c
typedef struct {
    const char* name;
    const char* version;
    int (*init)(void* core_api);
    int (*on_message)(const uint8_t* msg, size_t len);
} NumoPlugin;

int numo_plugin_register(const NumoPlugin* plugin);
```

**已规划插件**：神霁 (Lumora) AI 助手。

---

## 目录结构

```text
numotirus/
├── core/
│   ├── crypto/           ✅ 加密模块
│   ├── p2p/              ✅ P2P 网络层（KCP + select）
│   ├── protocol/         ✅ Noise 协议
│   ├── plugin/           ❌ 待实现
│   └── CMakeLists.txt
├── legacy/
│   └── yx/               ✅ yx 原始 P2P 实现存档
├── p2p_chat.c            ✅ 命令行聊天示例
├── docs/
│   └── ARCHITECTURE.md   ✅ 本文档
└── websites/             ✅ 项目官网
```

## 开发状态总览

| 模块 | 完成度 | 说明 |
| ------ | -------- | ------ |
| `core/crypto` | 100% | C API，稳定 |
| `core/p2p` | 100% | KCP + select，跨平台 |
| `core/protocol` (Noise) | 100% | Noise XX + SAS + TOFU |
| `p2p_chat.c` | 90% | 命令行示例，SAS 验证已实现 |
| `core/plugin` | 0% | 待实现 |
| GUI | 0% | 待实现 |

## 下一步优先级

1. **NAT 穿透** —— UPnP / 去中心化中继
2. **GUI 原型** —— Avalonia + C FFI
3. **神霁插件** —— Python FFI 集成

## 贡献指南

- **核心代码**：C11 / C++20，通过 `clang-format`
- **双语注释**：英文在前，中文在后
- **AI 使用**：允许，但必须理解每一行

## 许可证

Apache 2.0。核心协议永久开源。

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
| **加密层** | `core/crypto/` | C | X25519 + XChaCha20-Poly1305 + ECIES | 🔄 开发中 |
| **协议层** | `core/protocol/` | C++ | Noise XX 握手、SAS 验证 | ❌ 待实现 |
| **网络层** | `core/p2p/` | C | UDP + KCP 可靠传输，跨平台 | ❌ 待实现 |
| **应用层** | `cli/`, `p2p_chat.c` | C++ / C | 命令行测试工具 | ❌ 待实现 |
| **插件层** | `core/plugin/` | C (ABI) | 动态加载扩展 | ❌ 待实现 |
| **GUI 层** | `gui/` | C# / Avalonia | 跨平台图形界面 | ❌ 待实现 |

## 核心模块详解

### 加密层 (`core/crypto/`)

**状态**：🔄开发中

**功能**：

- X25519 密钥对生成、密钥交换
- XChaCha20-Poly1305 认证加密
- ECIES 公钥加密（`crypto_encrypt_public` / `crypto_decrypt_private`）
- BLAKE2b 密钥派生

**依赖**：`libsodium`

---

### 网络层 (`core/p2p/`)

**状态**：🔄开发中

**功能**：

- UDP + KCP 可靠传输（重传、有序、拥塞控制）
- `select()` 非阻塞接收，线程安全退出
- 跨平台（Windows/Linux）

**依赖**：`kcp`（submodule）

---

### 协议层 (`core/protocol/`)

**状态**：：🔄开发中

**功能**：

- Noise XX 模式握手（交互式，双方身份隐藏）
- X25519 密钥交换
- SAS（短认证字符串）生成与比对
- 信任存储（TOFU）

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

**已规划插件**：轻量级本地 AI 助手、盖格计数器支持、全球地图预览。

---

## 目录结构

```text
numotirus/
├── core/
│   ├── crypto/           🔄 加密模块
│   ├── p2p/              ❌ P2P 网络层（KCP + select）
│   ├── protocol/         ❌ Noise 协议
│   ├── plugin/           ❌ 待实现
├── legacy/
│   └── yx/               ✅ yx 原始 P2P 实现存档
│   └── domirus-lumora/   ✅ P2P 实现、ZRTP 草案
├── docs/
│   └── ARCHITECTURE.md   ✅ 本文档
└── websites/             ✅ 项目官网
```

## 开发状态总览

| 模块 | 完成度 | 说明 |
| ------ | -------- | ------ |
| `core/crypto` | 20% | 全力开发中 |
| `core/p2p` | 0% | KCP + select，跨平台 |
| `core/protocol` (Noise) | 0% | Noise XX + SAS + TOFU |
| `p2p_chat.c` | 0% | 命令行示例 |
| `core/plugin` | 0% | 待实现 |
| GUI | 0% | 待实现 |

## 下一步优先级

1. **P2P 核心代码** —— 挑战局域网连接
2. **Noise 协议** —— 安全交换密钥
3. **P2P 命令行测试** —— 确保方向没有偏差
4. **NAT 穿透** —— UPnP / 去中心化中继
5. **GUI 原型** —— Avalonia + C FFI
6. **神霁插件** —— Python FFI 集成

## 贡献指南

参看[这份代码规范文档](./CODING_STYLE.md)和[协作指南](./CONTRIBUTING.md)。

## 许可证

Apache 2.0。核心协议永久开源。

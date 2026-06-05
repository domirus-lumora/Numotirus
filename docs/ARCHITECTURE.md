# Numotirus 架构

本文档面向希望理解或贡献核心代码的开发者。

## 设计原则

1. **模块化**：协议层、网络层、加密层、插件层严格分离，每层可独立替换。
2. **够用就好**：先跑通核心流程，不追求过度设计。第一个目标：两人在命令行发加密消息。
3. **开源优先**：核心协议 Apache 2.0，插件生态可灵活扩展。
4. **沉默优先**：默认不广播、不心跳、不留痕迹。连接是临时策略，不是常态。

## 分层架构

| 层级 | 目录 | 语言 | 职责 | 状态 |
| ------ | ------ | ------ | ------ | ------ |
| **应用层** | `cli/`, `gui/` | C++ / C# | 命令行工具、图形界面 | ❌ 待实现 |
| **插件层** | `core/plugin/` | C (ABI) | 动态加载插件（神霁、离线信标等） | ❌ 待实现 |
| **协议层** | `core/protocol/` | C++ | 消息格式、会话管理、握手逻辑 | ❌ 待实现 |
| **网络层** | `core/p2p/` | C++ | 封装 libp2p：节点发现、连接、流式传输 | ❌ 待实现 |
| **加密层** | `core/crypto/` | C++ | 密钥交换、公钥加密、对称加密、签名 | ✅ **已完成** |

## 核心模块详解

### 加密层 (`core/crypto/`)

**状态**：✅ 稳定，提供单元测试 (`crypto_test.cpp`)。

**功能**：

- X25519 密钥对生成、密钥交换
- XChaCha20-Poly1305 认证加密
- ECIES 公钥加密（`encrypt_public` / `decrypt_private`）
- BLAKE2b 密钥派生
- 随机字节生成

**依赖**：`libsodium`

**贡献**：欢迎审阅代码，寻找潜在漏洞或性能问题。

```cpp
// 使用示例
auto kp = generate_keypair();
auto cipher = encrypt_public("hello", recipient_pubkey);
auto plain = decrypt_private(cipher, my_secretkey);
```

---

### 网络层 (`core/p2p/`) - 下一阶段核心

**目标**：基于 `libp2p` 实现，提供节点发现、连接管理、流式传输。

**任务**：

1. 集成 `cpp-libp2p`，跑通最小 echo 示例
2. 封装 `Node` 类，提供 `connect(peer_id)` 和 `send(stream, data)`
3. 提供 C ABI，供插件层调用

```cpp
// 设计接口（草案）
class P2PNode {
    void start();
    void connect(std::string peer_id);
    void send(std::string msg);
    void onMessage(std::function<void(std::string)> cb);
};
```

---

### 插件层 (`core/plugin/`) - 生态关键

**接口**：使用 `extern "C"` 保证 ABI 稳定，任何语言可写插件。

```c
typedef struct {
    const char* name;
    const char* version;
    int (*init)(void* core_api);
    int (*on_message)(const uint8_t* msg, size_t len);
} NumoPlugin;

int numo_plugin_register(const NumoPlugin* plugin);
int numo_plugin_load(const char* path);
```

**权限模型**：插件需声明所需权限（网络、文件、麦克风），用户安装时确认。

**已规划插件**：

| 插件 | 功能 | 语言 |
| ------ | ------ | ------ |
| 神霁 (Shenji) | AI 语音翻译助手 | Python (C FFI) |
| 离线信标 | BLE 广播求救、位置同步 | C++ |
| 家庭守护者 | 老人/儿童安全监护 | C++ |

---

### 协议层 (`core/protocol/`)

**消息格式**（草案）：

| 类型 | 格式 | 说明 |
| ------ | ------ | ------ |
| 握手 | `[type=1][version][pubkey][nonce][signature]` | 建立会话，交换公钥 |
| 数据 | `[type=2][session_id][ciphertext][tag]` | 加密后的消息体 |
| 信标 | `[type=3][location][timestamp][signature]` | 离线求救广播 |
| 心跳 | `[type=4][timestamp][signature]` | 可选，默认关闭 |

**会话管理**：

- 超时默认 5 分钟无活动自动销毁
- 支持会话恢复（用共享秘密派生新密钥，无需重新握手）

## 安全模型

| 攻击 | 防护 |
| ------ | ------ |
| 窃听 | XChaCha20-Poly1305 加密 |
| 篡改 | Poly1305 认证标签 |
| 中间人 | 公钥固定 (TOFU) + 签名验证 |
| 重放 | 会话计数器 |
| 流量分析 | 填充消息（可选，默认关闭） |
| 节点冒充 | 私钥签名 + 公钥验证 |

**核心原则**：

- 私钥永不离开本地
- 不信任网络、不信任节点、不信任时间
- 所有验证用本地数学完成

## 目录结构

```text
numotirus/
├── core/
│   ├── crypto/           ✅ 加密模块（已完成）
│   ├── p2p/              ❌ 待实现
│   ├── protocol/         ❌ 待实现
│   ├── plugin/           ❌ 待实现
│   └── CMakeLists.txt
├── cli/                  ❌ 待实现
├── gui/                  ❌ 待实现（Avalonia C#）
├── bindings/             ❌ 待实现（Python、Rust、D）
├── docs/
│   ├── ARCHITECTURE.md   ✅ 本文档
│   └── RFC/              ❌ 待补充（离线信标等）
└── websites/             ✅ 项目官网
```

## 开发状态总览

| 模块 | 完成度 | 缺失 |
| ------ | -------- | ------ |
| `core/crypto` | 100% | — |
| `SKILL/tct` | 100% | — |
| 网站 | 100% | — |
| CI/CD | 100% | — |
| 文档框架 | 90% | RFC 待补充 |
| `core/p2p` | 0% | 全部 |
| `core/protocol` | 0% | 全部 |
| `core/plugin` | 10% | 动态加载、示例插件 |
| CLI | 0% | 全部 |
| GUI | 0% | 全部 |

## 下一步优先级

1. **P2P 节点发现 + 连接**（集成 libp2p）—— 3-5 天
2. **Session 管理 + 消息格式** —— 2 天
3. **CLI 最小可运行版本** —— 2 天
4. **插件系统动态加载** —— 2 天
5. **神霁插件（Python FFI）** —— 3 天

预计 **2 周后**，两个人能在命令行发加密消息。

## 贡献指南

- **核心代码**：必须 C++20，通过 `CODING_STYLE.md` 检查，有单元测试
- **SKILL 分支**：任何语言，原型即可，不进 main
- **插件**：功能稳定后，从 SKILL 迁移，通过 C ABI 接入

## 许可证

Apache 2.0。核心协议永久开源。插件可独立选择许可证。

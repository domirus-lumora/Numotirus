# NUMOTIRUS 项目

## 代码质量与安全审计报告

**报告日期：** 2026-07-07  
**审计机构：** DeepSeek AI 安全审计部  
**审计方法：** 静态代码分析、架构审查、威胁建模  
**密级：** 非密 — 仅供技术评估使用

---

## 一、执行摘要

本报告对 Numotirus 项目（一个去中心化通信协议实现）进行了全面的技术审计。审计范围涵盖以下模块：加密模块（C++ 与 C API）、P2P 网络层、DHT 路由表、NAT 穿透、ZRTP 协议实现、TCT 混淆模块及传输层。

基于分析，该项目目前不符合生产部署标准。主要问题集中在三个方面：

1. **协议选型问题：** 使用 ZRTP 进行密钥交换，该协议已被主要开源项目标记为废弃，且在学术文献和政府披露中均记载了已知的安全局限。
2. **实现质量问题：** P2P 层存在并发问题、资源管理错误及潜在的内存安全违规。
3. **系统集成问题：** C 与 C++ API 互不兼容，且多个子系统（ICE、NAT 穿透）实现不完整。

本报告为每项发现提供详细证据，并提出修正建议。

---

## 二、项目背景与审计范围

**代码库：** 已提供全部源文件  
**编程语言：** C11 与 C++20 混合  
**主要依赖：** libsodium、KCP、libjuice（封装层）、pthread/Win32 线程

### 审计模块清单

| 模块 | 文件 | 功能描述 |
| ------ | ------ | ---------- |
| 加密模块 | crypto.cpp, crypto.hpp, crypto_c.c, crypto_c.h | X25519 密钥交换、XChaCha20-Poly1305 AEAD |
| P2P 网络 | p2p.cpp, p2p.h, p2p_chat.cpp, p2p_core.cpp, p2p_session.cpp | 网络节点管理、消息路由 |
| DHT | dht.cpp, dht.hpp, dht_c.cpp, dht_c.h | Kademlia DHT 路由表 |
| NAT 穿透 | nat_stun.cpp/hpp, nat_traversal.cpp/hpp, udp_hole_punch.cpp/hpp, port_prediction.cpp/hpp | NAT 穿透工具集 |
| ZRTP | zrtp.c, zrtp.h | 密钥交换协议实现 |
| TCT | tct.cpp, tct.hpp | 基于 EML 的数据变换 |
| 传输层 | transport.cpp, transport.hpp | DHT/Mesh 路由抽象层 |

---

## 三、审计方法

本次审计采用以下方法：

- **静态代码审查：** 人工检查全部源文件，重点关注逻辑错误、内存管理和并发正确性。
- **架构分析：** 检查模块间的交互关系、API 设计和数据流。
- **威胁建模：** 对照已知攻击向量评估密码协议选型。
- **交叉验证：** 验证 C 与 C++ 实现间的 API 一致性。

本次审计未进行动态测试或运行时分析。

---

## 四、详细发现

### 4.1. 协议选型：ZRTP

**观察：** 项目实现了 ZRTP（Zimmermann 实时传输协议）进行密钥交换，包括 SAS 生成和信任存储。

**评估：**

- **行业废弃状态：** 主要 ZRTP 实现库（libzrtpcpp）已被多个发行版标记为废弃，FreeBSD ports 已设置过期日期。FreeSWITCH 项目因上游已停止维护而移除了 ZRTP 支持。
- **已知局限：** 学术分析已识别出 SAS 验证机制在主动攻击者模型下的漏洞。该协议依赖人工带外验证，可能通过社会工程学或 AI 辅助的语音合成被绕过。
- **可用替代方案：** 主流安全通信平台（Signal、Wire、WebRTC 实现）已迁移至 DTLS-SRTP，该协议具有持续维护和标准化支持。

**建议：** 将 ZRTP 替换为有活跃维护和社区支持的协议，如 DTLS-SRTP 或 Noise Protocol Framework。

---

### 4.2. 加密模块：API 不兼容

**观察：** 项目同时提供 C++ 和 C 两种加密接口。两套实现的密钥派生函数不同。

**代码证据：**

C++ 版本（crypto.cpp）：

```cpp
std::array<uint8_t, KEY_SIZE> derive_key(
    const std::array<uint8_t, SHARED_SECRET_SIZE>& shared_secret,
    const std::string& salt) {
    // 追加 salt 后计算 BLAKE2b
}
// 调用方式：derive_key(shared, "ecies");
```

C 版本（crypto_c.c）：

```c
static void derive_key(const uint8_t* shared, uint8_t* key) {
    crypto_generichash(key, SYMMETRIC_KEY_SIZE, shared, SHARED_SECRET_SIZE, NULL, 0);
}
```

**影响：** `encrypt_public`（C++）生成的密文无法被 `decrypt_private`（C）解密，反之亦然。这使得 API 对任何可能同时使用两套接口的应用均不可用。

**建议：** 使用单一实现（建议采用 C++ 版本并实现 HKDF 风格的域分隔），并提供薄层 C 封装。

---

### 4.3. 加密模块：内存管理

**观察：** 临时密钥材料在函数返回前未显式清零。

**代码证据（crypto.cpp）：**

```cpp
std::array<uint8_t, SECRET_KEY_SIZE> ephemeral_secret;
std::array<uint8_t, PUBLIC_KEY_SIZE> ephemeral_public;
crypto_box_keypair(ephemeral_public.data(), ephemeral_secret.data());
// ... 使用 ephemeral_secret ...
// 函数退出前未调用 sodium_memzero
```

**影响：** 敏感的密码材料可能在函数返回后仍驻留内存，可能出现在核心转储中或被其他进程读取。

**建议：** 在返回前调用 `sodium_memzero(ephemeral_secret.data(), ephemeral_secret.size())`。

---

### 4.4. P2P 层：线程同步

**观察：** `P2PNode` 结构体包含多个在不同线程中访问的标志位，未使用显式同步。

**代码证据（p2p.cpp）：**

```cpp
struct P2PNode {
    volatile int running;        // 主线程修改，接收线程读取
    int peer_ready;              // 用户设置，发送线程读取
    int zrtp_exchanging;         // 用户和回调均可能修改
};
```

**技术分析：** `volatile` 关键字不提供原子性或内存顺序保证。对上述变量的并发读写构成 C++ 内存模型下的数据竞争，导致未定义行为。

**建议：** 对这些标志位使用 `std::atomic<int>`，或使用互斥锁保护所有访问。

---

### 4.5. P2P 层：释放后使用风险

**观察：** 接收回调捕获了指向 `P2PNode` 实例的裸指针。

**代码证据（p2p.cpp）：**

```cpp
P2PNode* raw = n;
n->transport->SetReceiveCallback(
    [raw](const NodeId& from, const std::string& src_ip, uint16_t src_port,
          const uint8_t* data, size_t len) {
        if (raw && raw->on_message) {
            raw->on_message(src_ip.c_str(), src_port, data, len);
        }
    }
);
```

**风险：** 如果 `P2PNode` 在 `transport` 移除回调之前被销毁，lambda 可能在被释放的对象上被调用。`raw && raw->on_message` 检查无法防止释放后使用，它仅检查空指针。在 `free(n)` 调用后指针立即失效。

**建议：** 使用 `std::weak_ptr` 或确保在对象销毁期间、释放内存之前移除回调。

---

### 4.6. P2P 层：资源管理

**观察：** 初始化函数中的多个错误路径未完全清理资源。

**代码证据（p2p.cpp）：**

```cpp
n->kcp = ikcp_create(KCP_CONV, &n->kcp_ctx);
if (!n->kcp) {
    CLOSE(n->sock);
    free(n);
    return NULL;
}
```

**问题：** 此时互斥锁（`kcp_mutex`）已被初始化，但此错误路径中未销毁该互斥锁。在 Windows 上会泄漏同步对象句柄。

**建议：** 对所有系统资源使用 RAII 封装，或确保每个错误路径执行完整的资源清理。

---

### 4.7. P2P 层：DHT 状态重复

**观察：** P2P 模块创建了自己的 DHT 实例，而 Transport 模块在内部创建了独立的 DHT 实例。

**代码路径：**

1. `p2p_create` → `dht_create(dht_id)`（C API）
2. `p2p_create` → `std::make_unique<CombinedTransport>(...)` → `DhtTransport` → 内部 `DhtClient`

**影响：** 通过传输层 DHT 发现的节点不会被添加到主 DHT 句柄，反之亦然。这导致路由表不一致，P2P 节点发现机制失效。

**建议：** 使用单一 DHT 实例供所有组件共享，或设计传输层将发现的节点转发到主路由表。

---

### 4.8. NAT 穿透模块：ICE 实现不完整

**观察：** `TryIce` 方法不包含任何功能性 ICE 逻辑。

**代码证据（nat_traversal.cpp）：**

```cpp
bool NatTraversal::Impl::TryIce(const Candidate& target) {
#ifdef HAVE_LIBJUICE
    std::cout << "[NAT] ICE to " << target.ip << ":" << target.port << "\n";
    // 占位：实际 ICE 实现应在此处
    return false;
#else
    std::cout << "[NAT] ICE unavailable (libjuice not compiled)\n";
    return false;
#endif
}
```

**影响：** 尽管项目中包含了 `libjuice_wrapper.cpp/hpp`，ICE 实际上并未实现。`p2p_nat_start_traversal` 函数也只是记录日志，未采取进一步行动来建立连接。

**建议：** 完成 ICE 实现，或移除占位代码。如果 ICE 不在计划之内，应移除对 libjuice 的依赖。

---

### 4.9. DHT 模块：引导节点管理

**观察：** 引导节点列表在源码中硬编码。

**代码证据（dht_c.cpp）：**

```cpp
std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes = {
    {"dht.transmissionbt.com", 6881},
    {"dht.libtorrent.org", 25401},
    // ... 20 余个条目
};
```

**影响：** 如果上述服务变更地址或不可用，DHT 无法引导。缺少动态更新或基于 DNS 的发现机制。

**建议：** 支持基于 DNS 的引导（使用 SRV 记录或类似机制），并允许运行时配置引导节点。

---

### 4.10. TCT 模块：冗余安全层

**观察：** Tesla Coil Transform 在明文和 AEAD 加密之间提供了一个可逆变换层。

**技术分析：** 模块自身文档说明："TCT 本身不提供安全性。安全性由外层的 XChaCha20-Poly1305（AEAD）保证。"

该变换使用固定 `secret` 和时间戳 `now`：

- `r = ln(now + secret)`
- 三层 EML 变换：`exp(x) - ln(w)`

如果 AEAD 是安全的，此变换不增加任何安全价值。如果 AEAD 被攻破，由于该变换是确定性的且给定 secret 和时间戳即可逆，因此不能保护明文。

**影响：** 增加了计算开销、量化误差和攻击面，而无相应的安全收益。

**建议：** 移除 TCT 模块，直接使用 AEAD 加密。

---

### 4.11. 传输层：同步解析

**观察：** `DhtTransport::Resolve` 方法在返回结果前会阻塞调用线程。

**代码证据（transport.cpp）：**

```cpp
std::optional<std::pair<std::string, uint16_t>> DhtTransport::Resolve(const NodeId& target) {
    auto nodes = impl_->client_->IterativeFindNode(target, 8);
    // ... 阻塞调用
}
```

**影响：** 在单线程或线程数受限的环境中，这可能阻塞调用方直到 DHT 查找完成（可能持续数秒）。这在实时应用中可能不可接受。

**建议：** 考虑同时提供同步和异步（基于回调）的解析方法。

---

### 4.12. ZRTP 实现：功能正确但协议已废弃

**观察：** `zrtp.c` 正确使用了 libsodium 的 `crypto_kx`，并通过 BLAKE2b 生成 SAS。信任存储（基于文件）功能完整。

**评估：** 该实现对于 ZRTP 规范而言在技术上是正确的。但协议规范本身已被废弃且存在已知的安全局限（见 4.1 节）。实现质量无法弥补协议层面的问题。

**建议：** 按照 4.1 节建议，迁移至有维护的协议。

---

## 五、评估汇总表

| 模块 | 实现质量 | 集成状态 | 安全性 | 总体评价 |
| ------ | ---------- | ---------- | -------- | ---------- |
| 加密（C++） | 一般 | 与 C API 不兼容 | 算法正确，实践不足 | 需修改 |
| 加密（C） | 一般 | 与 C++ API 不兼容 | 密钥派生不一致 | 需修改 |
| P2P | 差 | 与 DHT/Transport 耦合严重 | 并发问题、释放后使用 | 需重写 |
| DHT | 良好 | 与 P2P DHT 重复 | 不支持 IPv6 | 需重构 |
| NAT/STUN | 良好 | ICE 不完整 | 测试不足 | 需完成 |
| NAT/Traversal | 部分 | 回调未连接 | 预测算法为实验性质 | 需集成 |
| ZRTP | 良好 | 独立模块 | 协议已废弃 | 需替换 |
| TCT | 优秀 | 独立模块 | 不增加安全性 | 建议移除 |
| 传输层 | 一般 | DHT 状态重复 | 同步阻塞 | 需重构 |

---

## 六、改进建议

### 即时（阻塞级）

1. **替换 ZRTP** 为 DTLS-SRTP 或 Noise Protocol Framework。当前协议已废弃且存在已知漏洞。

2. **统一 C 和 C++ 加密 API**，使用相同的密钥派生方案。当前的不一致使模块在混合语言应用中不可用。

3. **修复 P2P 层的线程安全问题**，使用 `std::atomic` 管理标志位，确保所有共享状态有适当的互斥锁保护。

4. **消除接收回调中的释放后使用风险**，使用弱引用或确保回调生命周期管理。

### 短期

1. **完成 ICE 实现**，如果 NAT 穿透是必要功能，否则移除占位代码。

2. **合并 DHT 实例**，使单一路由表在 P2P 层和传输层间共享。

3. **增加密钥材料的零化处理**。

4. **移除 TCT 模块**，除非有合理的业务场景证明其开销是必要的。

### 长期

1. **定义正式威胁模型**，明确项目的攻击者假设和安全保证。

2. **通过可扩展接口支持后量子密钥封装**。

3. **实现全面的单元测试和集成测试**，覆盖线程、错误路径和密码学边界情况。

4. **建立安全审计周期**，用于所有未来发布版本。

---

## 七、审计结论

Numotirus 项目展现了若干实现良好的组件（DHT 路由、STUN 客户端、数值变换），但这些被架构层面的根本问题所抵消。ZRTP 的选型和加密 API 的不一致是最重大的问题。P2P 层的并发和内存安全问题在生产环境中很可能导致崩溃或安全漏洞。

基于当前代码库，该项目不建议用于生产部署。通过大规模重构和推荐的协议变更，项目可以达到可用状态。所需工作量较大，很可能需要重新设计 P2P 层和传输层。

---

**报告编制：** DeepSeek AI 安全审计部  
**日期：** 2026-07-07  
**密级：** 技术报告 — 无保密限制  
**分发范围：** 不限

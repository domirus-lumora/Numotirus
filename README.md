# Numotirus —— 信使协议

[English version](./ENG_README.md)

**去中心化密钥交换 · 无中央服务器 · 开源通信基础设施**
Numotirus 不是一个“聊天软件”。它是一个通信协议。没有公司运营服务器，没有平台审核内容，没有年龄限制。你持有密钥，你建立连接，你决定谁可以找到你。

它不是“社交媒体”。它是**社交基础设施**。

📖 **[读这篇演讲稿](./docs/ENG_humanity.md)** —— 一个关于连接、灾难和信任的独白。

## 为什么需要 Numotirus？

因为中心化平台正在控制对话。因为年龄、身份、地理位置正在成为“说话”的门槛。因为代码不应该被“禁止”，代码应该被“共建”。

## 它能做什么？

| 功能 | 说明 |
| ------ | ------ |
| **密钥交换** | Noise 协议，XX 模式握手 + SAS 比对，你持有自己的身份 |
| **P2P 通信** | UDP + KCP 可靠传输，消息直连，无中央服务器 |
| **开源** | Apache 2.0，任何人都可以查看、修改、分发 |
| **可嵌入** | 未来神霁（Lumora）将成为默认助手 |

## 技术栈

- **核心协议**：C++20 + C11（跨平台）
- **网络层**：自研 P2P 栈（UDP + KCP + select 非阻塞）
- **加密**：libsodium（X25519 + XChaCha20-Poly1305 + Noise）
- **GUI**：C# / Avalonia（跨平台，开发中）

## 这不是一个人项目

这是开源的。这是社区的。这是**你的**。

你不需要“注册”，你只需要“下载”。
你不需要“被审核”，你只需要“持有密钥”。
你不需要“等待许可”，你只需要“运行代码”。

## 当前状态

🚧 核心协议和 P2P 网络层需要你的帮助，欢迎贡献。

- [ ] 密钥交换协议（Noise XX + SAS）
- [ ] P2P 网络层（UDP + KCP + 跨平台）
- [ ] GUI 客户端原型
- [ ] 神霁集成

## 许可证

Apache 2.0。你可以用它、改它、把它用在你的项目里。只需要[保留版权声明](./LICENSE)。

## 加入我们

这不是“求救”。是“召集”。

- **代码仓库**：[https://github.com/domirus-lumora/Numotirus](https://github.com/domirus-lumora/Numotirus)
- **Issues**：[https://github.com/domirus-lumora/Numotirus/issues](https://github.com/domirus-lumora/Numotirus/issues)
- **讨论**：[点击加入](https://discord.com/invite/KdjnEtpSP8)

你不是在“帮影域”。你是在建一个更好的互联网。

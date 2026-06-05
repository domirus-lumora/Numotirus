# P2P Encrypted Chat Module

端到端加密的 P2P 命令行聊天模块。

## Features / 功能

- X25519 密钥交换 + XChaCha20-Poly1305 认证加密 (ECIES)
- UDP 直连，无中央服务器
- 支持中文、英文、Emoji 等 UTF-8 字符
- Windows / Linux 跨平台支持

## Dependencies / 依赖

- [libsodium](https://doc.libsodium.org/) 1.0.18+

## Build / 编译

### Windows (MSYS2 / MinGW)

```bash
gcc -c crypto_c.c -o crypto_c.o -lsodium
gcc -c p2p.c -o p2p.o -lws2_32
gcc -c p2p_chat.c -o p2p_chat.o -I.
gcc -o p2p_chat.exe p2p_chat.o p2p.o crypto_c.o -lsodium -lws2_32 -lpthread
```

### Linux

```bash
gcc -c crypto_c.c -o crypto_c.o -lsodium
gcc -c p2p.c -o p2p.o
gcc -c p2p_chat.c -o p2p_chat.o -I.
gcc -o p2p_chat p2p_chat.o p2p.o crypto_c.o -lsodium -lpthread
```

## Usage / 使用

### 终端1 (监听)

```bash
./p2p_chat.exe 8888
```

### 终端2 (发送)

```bash
./p2p_chat.exe 8889 127.0.0.1 8888
```

### 交互命令

| 命令 | 说明 |
| ------ | ------ |
| `/key <64hex>` | 设置对方公钥（启用加密） |
| `/peer <ip> <port>` | 设置对方地址 |
| `/exit` | 退出 |
| 其他文字 | 发送消息 |

### 示例

1. 终端1 启动后显示自己的公钥
2. 终端2 启动后输入 `/key <终端1的公钥>`
3. 终端2 输入消息，终端1 即可收到

## Architecture / 架构

```text
┌─────────────────────────────────────────────────┐
│  p2p_chat.c (CLI)                               │
├─────────────────────────────────────────────────┤
│  p2p.h / p2p.c (P2P 节点)                       │
│  - UDP socket 管理                               │
│  - 接收线程                                      │
│  - 密钥交换                                      │
├─────────────────────────────────────────────────┤
│  crypto_c.h / crypto_c.c (加密层)                │
│  - X25519 密钥对生成                             │
│  - ECIES 公钥加密                                │
│  - XChaCha20-Poly1305 认证加密                   │
└─────────────────────────────────────────────────┘
```

## Protocol / 协议

### 消息格式

| 字段 | 大小 | 说明 |
| ------ | ------ | ------ |
| ephemeral_public | 32 字节 | 临时公钥 |
| nonce | 24 字节 | 随机数 |
| ciphertext | 变长 | 加密后的消息 + 16 字节认证标签 |

### 加密流程

1. 发送方生成临时密钥对
2. 计算共享秘密: `ephemeral_secret × recipient_public`
3. 派生对称密钥: `BLAKE2b(shared_secret)`
4. 用 XChaCha20-Poly1305 加密明文
5. 组装: `ephemeral_public + nonce + ciphertext`

## Security / 安全性

- 端到端加密，无中间人
- 前向安全（临时密钥对）
- 认证加密，防止篡改
- 私钥永不离开本地

## License / 许可证

Apache 2.0

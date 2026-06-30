# Numotirus 使用指南

Numotirus 是一个点对点加密通信协议。没有服务器、没有账号、没有注册。

---

## 你需要准备

- 两台在同一网络下的电脑 / 设备，或者
- 两台有互联网连接的设备（需要 NAT 穿透）
- 对方的公钥（64位十六进制）

---

## 编译脚本说明

Numotirus 使用 Shell 脚本编译——没有 CMake。根据你的平台选择：

| 脚本 | 平台 | 说明 |
| ------ | ------ | ------ |
| `build.bat` | Windows | MinGW / MSYS2 编译 |
| `build.sh` | Linux / macOS / Termux | 通用编译脚本 |
| `build_android.sh` | Android (Termux) | Android 优化编译 |
| `build_ios.sh` | iOS | 在 macOS 上交叉编译 iOS |
| `build_all.sh` | 所有平台 | 自动检测并执行对应脚本 |

---

## 安装编译

### Windows

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
build.bat
```

### Linux / macOS / Termux

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build.sh
./build.sh
```

### Android (Termux)

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build_android.sh
./build_android.sh
```

### iOS（需要 macOS + Xcode）

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build_ios.sh
./build_ios.sh
```

会生成 `libnumotirus.a`，可在 Xcode 项目中链接使用。

### 自动检测（任何平台）

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build_all.sh
./build_all.sh
```

---

## 运行

### 终端1（监听）

```bash
./p2p_chat 8888
```

你会看到自己的公钥：

```text
My public key: 3e85e9e798196d8048c460fdbd8e76c5980dc8771018c1d24001fe6bb8603b40
```

### 终端2（发送）

```bash
./p2p_chat 8889 127.0.0.1 8888
```

提示时输入终端1的公钥。

---

## 命令

| 命令 | 说明 |
| ------ | ------ |
| `/key <64hex>` | 设置对方公钥（启用加密） |
| `/peer <ip> <port>` | 设置对方地址 |
| `/dht` | 查看 DHT 路由表 |
| `/exit` | 退出 |
| 其他文字 | 作为消息发送 |

---

## 示例

**终端1：**

```bash
$ ./p2p_chat 8888
My public key: 3e85e9e7...
Enter peer public key (64 hex):
> (等待输入)
```

**终端2：**

```bash
$ ./p2p_chat 8889 127.0.0.1 8888
Enter peer public key (64 hex):
> 3e85e9e7...
> 你好
```

终端1 收到：`[127.0.0.1:8889] 你好`

---

## NAT 穿透

如果你和对方在不同网络下，Numotirus 会自动尝试 NAT 穿透：

- 直接 UDP 直连
- UDP 打洞
- 端口预测（针对对称型 NAT）
- ICE（需要 libjuice）

你不需要做任何特殊操作。直接用 `/peer` 加上对方的公网 IP 即可。

```bash
/peer 203.0.113.45 8888
```

---

## 常见问题

### DHT 引导失败

稍等几秒再试一次。公共 BitTorrent DHT 节点有时响应较慢。

### 连接超时

- 检查防火墙设置（允许 UDP 入站/出站）
- 确保两台设备在同一网络下
- 如果跨网络，用 `/peer` 指定公网 IP（STUN 可帮忙获取）

### 未设置对方公钥

必须先运行 `/key` 或在启动时输入对方公钥，否则无法发送消息。

---

## 目录结构

```text
numotirus/
├── core/
│   ├── crypto/      加密层（X25519 + XChaCha20-Poly1305）
│   ├── p2p/         P2P 网络层 + KCP + NAT 穿透
│   └── protocol/    ZRTP 密钥交换
├── p2p_chat         命令行聊天程序
├── build.bat        Windows 编译脚本
├── build.sh         Linux/macOS/ARM 编译脚本
├── build_android.sh Android/Termux 编译脚本
├── build_ios.sh     iOS 交叉编译脚本
├── build_all.sh     自动检测编译脚本
└── HowToUseNumotirus_CN.md
```

---

## 许可证

Apache 2.0

# How to Use Numotirus

Numotirus is a peer-to-peer encrypted chat protocol. There is no server, no account, no registration.

---

## What you need

- Two computers / devices on the same network, or
- Two computers / devices with internet access (requires NAT traversal)
- The public key of the person you want to talk to

---

## Build Scripts Overview

Numotirus uses shell scripts for building — no CMake. Choose the one for your platform:

| Script | Platform | Description |
| -------- | ---------- | ------------- |
| `build.bat` | Windows | MinGW / MSYS2 build |
| `build.sh` | Linux / macOS / Termux | Universal build script |
| `build_android.sh` | Android (Termux) | Android-optimized build |
| `build_ios.sh` | iOS | Cross-compile on macOS for iOS |
| `build_all.sh` | All platforms | Auto-detects and runs the right script |

---

## Installation

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

### iOS (requires macOS with Xcode)

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build_ios.sh
./build_ios.sh
```

This generates `libnumotirus.a` for linking into an Xcode project.

### Auto-detect (any platform)

```bash
git clone https://github.com/domirus-lumora/Numotirus.git
cd Numotirus
chmod +x build_all.sh
./build_all.sh
```

---

## Running

### Terminal 1 (Listener)

```bash
./p2p_chat 8888
```

You will see your public key:

```text
My public key: 3e85e9e798196d8048c460fdbd8e76c5980dc8771018c1d24001fe6bb8603b40
```

### Terminal 2 (Sender)

```bash
./p2p_chat 8889 127.0.0.1 8888
```

When prompted, enter the public key from Terminal 1.

---

## Commands

| Command | Description |
| --------- | ------------- |
| `/key <64hex>` | Set peer's public key (enable encryption) |
| `/peer <ip> <port>` | Set peer's address |
| `/dht` | Show DHT routing table |
| `/exit` | Quit |
| any other text | Send as message |

---

## Example

**Terminal 1:**

```bash
$ ./p2p_chat 8888
My public key: 3e85e9e7...
Enter peer public key (64 hex):
> (waiting)
```

**Terminal 2:**

```bash
$ ./p2p_chat 8889 127.0.0.1 8888
Enter peer public key (64 hex):
> 3e85e9e7...
> hello
```

Terminal 1 receives: `[127.0.0.1:8889] hello`

---

## NAT Traversal

If you are on different networks, Numotirus will attempt NAT traversal automatically:

- Direct UDP
- UDP hole punching
- Port prediction (for symmetric NAT)
- ICE (if libjuice is available)

You do not need to do anything special. Just use `/peer` with the other person's public IP.

```bash
/peer 203.0.113.45 8888
```

---

## Troubleshooting

### DHT bootstrap fails

Wait a few seconds and try again. Public BitTorrent DHT nodes sometimes take time to respond.

### Connection timeout

- Check firewall settings (allow UDP inbound/outbound)
- Make sure both devices are on the same network
- For different networks, use `/peer` with the public IP (STUN can help)

### Peer key not set

You must run `/key` or enter the key on startup before you can send messages.

---

## File Structure

```text
numotirus/
├── core/
│   ├── crypto/      Encryption (X25519 + XChaCha20-Poly1305)
│   ├── p2p/         P2P network + KCP + NAT traversal
│   └── protocol/    ZRTP key exchange
├── p2p_chat         CLI chat application
├── build.bat        Windows build script
├── build.sh         Linux/macOS/ARM build script
├── build_android.sh Android/Termux build script
├── build_ios.sh     iOS cross-compile script
├── build_all.sh     Auto-detect build script
└── HowToUseNumotirus.md
```

---

## License

Apache 2.0

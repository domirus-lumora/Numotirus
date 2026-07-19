# Numotirus Architecture

This document is for developers who want to understand or contribute to the core code.

## Design Principles

1. **Modularity**: Crypto layer, network layer, protocol layer, and application layer are strictly separated.
2. **Just enough**: Get the core flow working first. First goal: two people exchanging encrypted messages via command line.
3. **Open source first**: Core protocol is Apache 2.0.
4. **Silence first**: No broadcasting, no heartbeat, no traces by default.

## Layered Architecture

| Layer | Directory | Language | Responsibility | Status |
| ------- | ----------- | ---------- | ---------------- | -------- |
| **Application** | `cli/`, `p2p_chat.c` | C++ / C | CLI test tool | ✅ Working |
| **Protocol** | `core/protocol/` | C++ | Noise XX handshake, SAS verification | ✅ Complete |
| **Network** | `core/p2p/` | C | UDP + KCP reliable transport, cross-platform | ✅ Complete |
| **Crypto** | `core/crypto/` | C | X25519 + XChaCha20-Poly1305 + ECIES | ✅ Complete |
| **Plugin** | `core/plugin/` | C (ABI) | Dynamic plugin loading | ❌ Pending |
| **GUI** | `gui/` | C# / Avalonia | Cross-platform GUI | ❌ Pending |

## Core Module Details

### Crypto Layer (`core/crypto/`)

**Status**: ✅ Stable, provides `crypto_c.h` C interface.

**Features**:

- X25519 key pair generation and key exchange
- XChaCha20-Poly1305 authenticated encryption
- ECIES public-key encryption (`crypto_encrypt_public` / `crypto_decrypt_private`)
- BLAKE2b key derivation

**Dependency**: `libsodium`

```c
// Usage example
CryptoKeypair* kp = crypto_keypair_generate();
crypto_encrypt_public(plain, len, peer_pubkey, &cipher, &clen);
crypto_decrypt_private(cipher, clen, my_seckey, &plain, &plen);
```

---

### Network Layer (`core/p2p/`)

**Status**: ✅ Complete, based on KCP + select implementation.

**Features**:

- UDP + KCP reliable transport (retransmission, ordering, congestion control)
- Non-blocking `select()` receive, thread-safe exit
- Cross-platform (Windows/Linux)

```c
// Usage example
P2PNode* node = p2p_create(8888);
p2p_set_peer_key(node, peer_pubkey);
p2p_start(node);
p2p_send(node, "127.0.0.1", 8888, (uint8_t*)"hello", 5);
```

---

### Protocol Layer (`core/protocol/`)

**Status**: ✅ Noise handshake complete.

**Features**:

- Noise XX pattern handshake (interactive, identity hiding for both parties)
- X25519 key exchange
- SAS (Short Authentication String) generation and verification
- Trust store (TOFU)

```cpp
// Usage example
NoiseSession session;
session.SetKeyPair(kp);
session.SetPeerPublic(peer_pub);
auto err = session.Handshake(true, write_cb, read_cb);
std::string sas = session.GetSas();
```

**Dependency**: `noise-cpp` (submodule)

---

### Plugin Layer (`core/plugin/`) - Key to Ecosystem

**Draft Interface**:

```c
typedef struct {
    const char* name;
    const char* version;
    int (*init)(void* core_api);
    int (*on_message)(const uint8_t* msg, size_t len);
} NumoPlugin;

int numo_plugin_register(const NumoPlugin* plugin);
```

**Planned Plugins**: Lumora (Shenji) AI assistant.

---

## Directory Structure

```text
numotirus/
├── core/
│   ├── crypto/           ✅ Crypto module
│   ├── p2p/              ✅ P2P network layer (KCP + select)
│   ├── protocol/         ✅ Noise protocol
│   ├── plugin/           ❌ Pending
│   └── CMakeLists.txt
├── legacy/
│   └── yx/               ✅ yx's original P2P implementation archive
├── p2p_chat.c            ✅ CLI chat example
├── docs/
│   └── ARCHITECTURE.md   ✅ This document
└── websites/             ✅ Project website
```

## Development Status

| Module | Completion | Notes |
| --------- | ------------ | ------- |
| `core/crypto` | 100% | C API, stable |
| `core/p2p` | 100% | KCP + select, cross-platform |
| `core/protocol` (Noise) | 100% | Noise XX + SAS + TOFU |
| `p2p_chat.c` | 90% | CLI example, SAS verification implemented |
| `core/plugin` | 0% | Pending |
| GUI | 0% | Pending |

## Next Steps (Priority Order)

1. **NAT traversal** — UPnP / decentralized relay
2. **GUI prototype** — Avalonia + C FFI
3. **Lumora plugin** — Python FFI integration

## Contribution Guide

- **Core code**: C11 / C++20, pass `clang-format`
- **Bilingual comments**: English first, Chinese after
- **AI usage**: Allowed, but you must understand every line

## License

Apache 2.0. Core protocol is permanently open source.

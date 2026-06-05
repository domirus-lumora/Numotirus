# Numotirus Architecture

This document is for developers who want to understand or contribute to the core code.

## Design Principles

1. **Modularity**: Protocol layer, network layer, crypto layer, and plugin layer are strictly separated. Each layer can be replaced independently.
2. **Just enough**: Get the core flow working first. Don't over-engineer. First goal: two people exchanging encrypted messages via command line.
3. **Open source first**: Core protocol is Apache 2.0. Plugin ecosystem can be flexibly extended.
4. **Silence first**: No broadcasting, no heartbeat, no traces by default. Connections are temporary strategies, not the norm.

## Layered Architecture

| Layer | Directory | Language | Responsibility | Status |
| ------- | ----------- | ---------- | ---------------- | -------- |
| **Application** | `cli/`, `gui/` | C++ / C# | CLI tool, GUI | ❌ Pending |
| **Plugin** | `core/plugin/` | C (ABI) | Dynamic plugin loading (Shenji, offline beacon, etc.) | ❌ Pending |
| **Protocol** | `core/protocol/` | C++ | Message format, session management, handshake logic | ❌ Pending |
| **Network** | `core/p2p/` | C++ | Wrapper for libp2p: peer discovery, connection, streaming | ❌ Pending |
| **Crypto** | `core/crypto/` | C++ | Key exchange, public-key encryption, symmetric encryption, signatures | ✅ **Complete** |

## Core Module Details

### Crypto Layer (`core/crypto/`)

**Status**: ✅ Stable, includes unit tests (`crypto_test.cpp`).

**Features**:

- X25519 key pair generation and key exchange
- XChaCha20-Poly1305 authenticated encryption
- ECIES public-key encryption (`encrypt_public` / `decrypt_private`)
- BLAKE2b key derivation
- Random byte generation

**Dependency**: `libsodium`

**Contribution**: Code review is welcome. Look for potential vulnerabilities or performance issues.

```cpp
// Usage example
auto kp = generate_keypair();
auto cipher = encrypt_public("hello", recipient_pubkey);
auto plain = decrypt_private(cipher, my_secretkey);
```

---

### Network Layer (`core/p2p/`) - Next Core Priority

**Goal**: Implement peer discovery, connection management, and streaming based on `libp2p`.

**Tasks**:

1. Integrate `cpp-libp2p` and get a minimal echo example working.
2. Wrap a `Node` class with `connect(peer_id)` and `send(stream, data)`.
3. Provide a C ABI for the plugin layer.

```cpp
// Draft interface
class P2PNode {
    void start();
    void connect(std::string peer_id);
    void send(std::string msg);
    void onMessage(std::function<void(std::string)> cb);
};
```

---

### Plugin Layer (`core/plugin/`) - Key to Ecosystem

**Interface**: Uses `extern "C"` for ABI stability. Plugins can be written in any language.

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

**Permission Model**: Plugins must declare required permissions (network, file, mic). User confirms during installation.

**Planned Plugins**:

| Plugin | Function | Language |
| -------- | ---------- | ---------- |
| Shenji | AI voice translation assistant | Python (C FFI) |
| Offline Beacon | BLE SOS broadcast, location sync | C++ |
| Family Guardian | Safety monitoring for elderly/children | C++ |

---

### Protocol Layer (`core/protocol/`)

**Message Format (Draft)**:

| Type | Format | Description |
| ------ | -------- | ------------- |
| Handshake | `[type=1][version][pubkey][nonce][signature]` | Establish session, exchange public keys |
| Data | `[type=2][session_id][ciphertext][tag]` | Encrypted message body |
| Beacon | `[type=3][location][timestamp][signature]` | Offline SOS broadcast |
| Heartbeat | `[type=4][timestamp][signature]` | Optional, disabled by default |

**Session Management**:

- Sessions expire after 5 minutes of inactivity.
- Session resumption is supported (derive new key from shared secret, no handshake needed).

## Security Model

| Attack | Defense |
| -------- | --------- |
| Eavesdropping | XChaCha20-Poly1305 encryption |
| Tampering | Poly1305 authentication tag |
| Man-in-the-Middle | Public key pinning (TOFU) + signature verification |
| Replay | Session counter |
| Traffic analysis | Message padding (optional, off by default) |
| Node impersonation | Private key signature + public key verification |

**Core Principles**:

- Private keys never leave the local device.
- Trust no network, no node, no time.
- All verification is done locally with math.

## Directory Structure

```text
numotirus/
├── core/
│   ├── crypto/           ✅ Complete
│   ├── p2p/              ❌ Pending
│   ├── protocol/         ❌ Pending
│   ├── plugin/           ❌ Pending
│   └── CMakeLists.txt
├── cli/                  ❌ Pending
├── gui/                  ❌ Pending (Avalonia C#)
├── bindings/             ❌ Pending (Python, Rust, D)
├── docs/
│   ├── ARCHITECTURE.md   ✅ This document
│   └── RFC/              ❌ Pending (offline beacon, etc.)
└── websites/             ✅ Project website
```

## Development Status

| Module | Completion | Missing |
| -------- | ------------ | --------- |
| `core/crypto` | 100% | — |
| `SKILL/tct` | 100% | — |
| Website | 100% | — |
| CI/CD | 100% | — |
| Documentation framework | 90% | RFCs |
| `core/p2p` | 0% | Everything |
| `core/protocol` | 0% | Everything |
| `core/plugin` | 10% | Dynamic loading, example plugin |
| CLI | 0% | Everything |
| GUI | 0% | Everything |

## Next Steps (Priority Order)

1. **P2P peer discovery + connection** (integrate libp2p) — 3-5 days
2. **Session management + message format** — 2 days
3. **Minimal CLI version** — 2 days
4. **Dynamic plugin loading** — 2 days
5. **Shenji plugin (Python FFI)** — 3 days

Estimated **2 weeks** until two people can exchange encrypted messages via the command line.

## Contribution Guide

- **Core code**: Must be C++20, pass `CODING_STYLE.md` checks, include unit tests.
- **SKILL branch**: Any language, prototypes only, never merged into `main`.
- **Plugins**: After stabilization, migrate from SKILL and integrate via C ABI.

## License

Apache 2.0. The core protocol is permanently open source. Plugins can choose their own licenses.

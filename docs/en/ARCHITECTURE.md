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
| **Crypto** | `core/crypto/` | C | X25519 + XChaCha20-Poly1305 + ECIES | 🔄 In progress |
| **Protocol** | `core/protocol/` | C++ | Noise XX handshake, SAS verification | ❌ Pending |
| **Network** | `core/p2p/` | C | UDP + KCP reliable transport, cross-platform | ❌ Pending |
| **Application** | `cli/`, `p2p_chat.c` | C++ / C | CLI test tool | ✅ Working |
| **Plugin** | `core/plugin/` | C (ABI) | Dynamic plugin loading | ❌ Pending |
| **GUI** | `gui/` | C# / Avalonia | Cross-platform GUI | ❌ Pending |

## Core Module Details

### Crypto Layer (`core/crypto/`)

**Status**: 🔄 In progress

**Features**:

- X25519 key pair generation and key exchange
- XChaCha20-Poly1305 authenticated encryption
- ECIES public-key encryption (`crypto_encrypt_public` / `crypto_decrypt_private`)
- BLAKE2b key derivation

**Dependency**: `libsodium`

---

### Network Layer (`core/p2p/`)

**Status**: ❌ Pending

**Features**:

- UDP + KCP reliable transport (retransmission, ordering, congestion control)
- Non-blocking `select()` receive, thread-safe exit
- Cross-platform (Windows/Linux)

---

### Protocol Layer (`core/protocol/`)

**Status**: ❌ Pending
**Features**:

- Noise XX pattern handshake (interactive, identity hiding for both parties)
- X25519 key exchange
- SAS (Short Authentication String) generation and verification
- Trust store (TOFU)

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

**Planned Plugins**: Light AI assistant, Geiger counter support, Global map preview.

---

## Directory Structure

```text
numotirus/
├── core/
│   ├── crypto/           🔄 Crypto module
│   ├── p2p/              ❌ P2P network layer (KCP + select)
│   ├── protocol/         ❌ Noise protocol
│   └── plugin/           ❌ Pending
├── legacy/
│   |── yx/               ✅ yx's original P2P implementation archive
|   └── domirus-lumora/   ✅ P2P's code (outdated) and ZRTP
├── docs/
│   └── ARCHITECTURE.md   ✅ This document
└── websites/             ✅ Project website
```

## Development Status

| Module | Completion | Notes |
| --------- | ------------ | ------- |
| `core/crypto` | 20% | In progress |
| `core/p2p` | 0% | KCP + select, cross-platform |
| `core/protocol` (Noise) | 0% | Noise XX + SAS + TOFU |
| `p2p_chat.c` | 0% | CLI example, SAS verification implemented |
| `core/plugin` | 0% | Pending |
| GUI | 0% | Pending |

## Next Steps (Priority Order)

1. **P2P's code** —— Connect with people who are in same WiFi
2. **Noise protocol** —— Exchange the key securely
3. **P2P CLI test** —— Test the P2P's function
4. **NAT** —— UPnP / Centreless connect
5. **GUI** —— Avalonia + C FFI
6. **Light AI extension** —— Python FFI integrate

## Contribution Guide

See these two document to know: [Coding](./ENG_CODING_STYLE.md) and [Contributing Rules](./ENG_CONTRIBUTING.md)

## License

Apache 2.0. Core protocol is permanently open source.

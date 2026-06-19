# P2P Encrypted Chat Module

End-to-end encrypted P2P command-line chat module.

## Features

- X25519 key exchange + XChaCha20-Poly1305 authenticated encryption (ECIES)
- UDP direct connection, no central server
- Kademlia DHT node discovery
- UTF-8 support (Chinese, English, Emoji, etc.)
- Cross-platform: Windows / Linux

## Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+

## Build

### Windows (MSYS2 / MinGW)

```bash
gcc -c core/p2p/kcp/ikcp.c -o ikcp.o
g++ -std=c++20 -c core/p2p/dht.cpp -o dht.o
g++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o
gcc -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto
gcc -c core/protocol/zrtp.c -o zrtp.o -Icore/protocol -Icore/crypto
gcc -c core/p2p/p2p.c -o p2p.o -Icore/p2p -Icore/crypto -Icore/protocol
gcc -c core/p2p/p2p_chat.c -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol
gcc -o p2p_chat.exe p2p_chat.o p2p.o dht.o dht_c.o zrtp.o crypto_c.o ikcp.o -lstdc++ -lsodium -lws2_32 -lpthread
```

### Linux

```bash
gcc -c core/p2p/kcp/ikcp.c -o ikcp.o
g++ -std=c++20 -c core/p2p/dht.cpp -o dht.o
g++ -std=c++20 -c core/p2p/dht_c.cpp -o dht_c.o
gcc -c core/crypto/crypto_c.c -o crypto_c.o -Icore/crypto
gcc -c core/protocol/zrtp.c -o zrtp.o -Icore/protocol -Icore/crypto
gcc -c core/p2p/p2p.c -o p2p.o -Icore/p2p -Icore/crypto -Icore/protocol
gcc -c core/p2p/p2p_chat.c -o p2p_chat.o -Icore/p2p -Icore/crypto -Icore/protocol
gcc -o p2p_chat p2p_chat.o p2p.o dht.o dht_c.o zrtp.o crypto_c.o ikcp.o -lstdc++ -lsodium -lpthread
```

## Usage

### Terminal 1 (Listener)

```bash
./p2p_chat.exe 8888
```

### Terminal 2 (Sender)

```bash
./p2p_chat.exe 8889 127.0.0.1 8888
```

### Commands

| Command | Description |
| --------- | ------------- |
| `/key <64hex>` | Set peer's public key (enable encryption) |
| `/peer <ip> <port>` | Set peer's address |
| `/dht` | Show DHT routing table |
| `/exit` | Quit |
| other text | Send message |

### Example

1. Terminal 1 starts, displays its public key
2. Terminal 2 starts, enters `/key <terminal1_public_key>`
3. Terminal 2 types a message, Terminal 1 receives it

## Architecture

```text
┌─────────────────────────────────────────────────┐
│  p2p_chat.c (CLI)                               │
├─────────────────────────────────────────────────┤
│  p2p.h / p2p.c (P2P Node)                       │
│  - UDP socket management                        │
│  - Receive thread                               │
│  - Key exchange                                 │
│  - DHT routing table                            │
├─────────────────────────────────────────────────┤
│  crypto_c.h / crypto_c.c (Crypto Layer)         │
│  - X25519 key pair generation                   │
│  - ECIES public key encryption                  │
│  - XChaCha20-Poly1305 authenticated encryption  │
└─────────────────────────────────────────────────┘
```

## Protocol

### Message Format

| Field | Size | Description |
| ------- | ------ | ------------- |
| ephemeral_public | 32 bytes | Ephemeral public key |
| nonce | 24 bytes | Random nonce |
| ciphertext | variable | Encrypted message + 16 byte tag |

### Encryption Flow

1. Sender generates ephemeral key pair
2. Compute shared secret: `ephemeral_secret × recipient_public`
3. Derive symmetric key: `BLAKE2b(shared_secret)`
4. Encrypt plaintext with XChaCha20-Poly1305
5. Assemble: `ephemeral_public + nonce + ciphertext`

## Security

- End-to-end encryption, no man-in-the-middle
- Forward secrecy (ephemeral key pairs)
- Authenticated encryption, tamper-proof
- Private keys never leave local device

## License

Apache 2.0

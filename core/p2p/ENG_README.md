# P2P Encrypted Chat Module

End-to-end encrypted P2P command line chat module.

## Features

- X25519 key exchange + XChaCha20-Poly1305 authenticated encryption (ECIES)
- UDP direct connection, no central server
- UTF-8 support (Chinese, English, Emoji, etc.)
- Cross-platform: Windows / Linux

## Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+

## Build

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
| `/exit` | Quit |
| other text | Send message |

### Example

1. Terminal 1 shows its public key on startup
2. Terminal 2: `/key <public_key_from_terminal_1>`
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

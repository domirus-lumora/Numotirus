# Numotirus Crypto Module

C++20 cryptography module providing X25519 key exchange, XChaCha20-Poly1305 authenticated encryption, and ECIES public-key encryption.

## Features

- Key pair generation (X25519)
- Public-key encryption (ECIES-style)
- Private-key decryption
- Symmetric encryption (XChaCha20-Poly1305)
- Key derivation (BLAKE2b)
- Random byte generation

## Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Test

```bash
ctest
```

## Usage Examples

### Public-Key Encryption

```cpp
#include "crypto.hpp"

auto keypair = numotirus::crypto::generate_keypair();
// share keypair.public_key with recipient

auto cipher = numotirus::crypto::encrypt_public(plaintext, recipient_public);
auto plain = numotirus::crypto::decrypt_private(cipher, my_secret);
```

### Symmetric Encryption

```cpp
auto key = numotirus::crypto::random_bytes(32);
auto nonce = numotirus::crypto::random_bytes(24);
auto cipher = numotirus::crypto::encrypt(plaintext, key, nonce);
auto plain = numotirus::crypto::decrypt(cipher, key, nonce);
```

## CLI Tool

Enable `-DBUILD_CRYPTO_CLI=ON` during CMake configuration to build `crypto_cli.exe`, an interactive encryption tool.

## Security Notice

This module passes unit tests but has **not undergone independent cryptographic audit**. Use in production at your own risk.

## License

Apache 2.0

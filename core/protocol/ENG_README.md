# Numotirus Noise Protocol Module

Key exchange implementation based on the Noise Protocol Framework, providing XX pattern handshake, SAS short authentication string generation, and trust storage.

---

## Features

- Noise XX pattern handshake (interactive, identity hiding for both parties)
- X25519 elliptic curve key exchange
- ChaCha20-Poly1305 symmetric encryption
- BLAKE2s hash function
- SAS (Short Authentication String) generation (4 groups of 4 digits)
- Manual SAS verification with state flag
- Trust storage (persistent public key + shared secret)
- No role dependency (based on Noise protocol standard)

---

## File Structure

```text
core/protocol/
├── noise.hpp          # Header file, public API (wrapper layer)
├── noise.cpp          # Implementation (wrapper layer)
└── noise-cpp/         # Noise protocol implementation (submodule, original repo)
    ├── noise.h
    ├── noise.cpp
    ├── monocypher.c/h
    └── rng_get_bytes.c/h
```

---

## Build

```bash
cd core/protocol
g++ -std=c++20 -c noise.cpp -I. -Inoise-cpp -o noise.o
g++ -std=c++20 -c noise-cpp/noise.cpp -Inoise-cpp -o noise_cpp.o
gcc -c noise-cpp/monocypher.c -Inoise-cpp -o monocypher.o
gcc -c noise-cpp/rng_get_bytes.c -Inoise-cpp -o rng_get_bytes.o
```

---

## Usage

```cpp
#include "core/protocol/noise.hpp"

using namespace numotirus::protocol::noise;

// 1. Generate key pair
auto kp = GenerateKeyPair();

// 2. Create session
NoiseSession session;
session.SetKeyPair(kp.value());
session.SetPeerPublic(peer_public_key);

// 3. Perform handshake
auto err = session.Handshake(
    true,  // initiator
    [](const uint8_t* data, size_t len) -> ErrorCode {
        // Send data to peer
        return ErrorCode::kSuccess;
    },
    [](uint8_t* buffer, size_t len) -> ErrorCode {
        // Receive data from peer
        return ErrorCode::kSuccess;
    }
);

if (err != ErrorCode::kSuccess) {
    // Handle error
}

// 4. Get SAS code for user out-of-band verification
std::string sas = session.GetSas();
std::cout << "SAS: " << sas << std::endl;

// 5. Mark as verified after user confirmation
session.MarkVerified();

// 6. Get session keys
auto rx_key = session.GetRxKey();
auto tx_key = session.GetTxKey();

// 7. Check if handshake is complete
if (session.IsHandshakeComplete()) {
    // Ready for encrypted communication
}
```

---

## Trust Store

```cpp
// Save trust (call after first successful verification)
TrustSave("peer_id", peer_public_key, shared_secret);

// Load trust (check on next connection)
std::array<uint8_t, kPublicKeySize> pubkey;
std::array<uint8_t, kSharedKeySize> secret;
if (TrustLoad("peer_id", pubkey, secret) == ErrorCode::kSuccess) {
    // Trust exists, skip SAS verification
}
```

---

## API Reference

| Function | Description |
| ---------- | ------------- |
| `GenerateKeyPair()` | Generate X25519 key pair |
| `NoiseSession()` | Create a session |
| `SetKeyPair()` | Set local key pair |
| `SetPeerPublic()` | Set peer's public key |
| `Handshake()` | Perform Noise XX handshake |
| `GetSas()` | Get SAS string |
| `MarkVerified()` | Mark as verified |
| `IsVerified()` | Check if peer is verified |
| `GetRxKey()` | Get receive key |
| `GetTxKey()` | Get transmit key |
| `IsHandshakeComplete()` | Check if handshake is complete |
| `TrustSave()` | Save trust entry |
| `TrustLoad()` | Load trust entry |

---

## Test

```bash
# Build and run tests
g++ -std=c++20 -o test_noise test_noise.cpp noise.cpp noise-cpp/noise.cpp monocypher.c rng_get_bytes.c -I. -Inoise-cpp -lsodium
./test_noise
```

Expected output:

```bash
=== Noise Module Test ===

✅ test_keypair_generation: PASSED
✅ test_handshake: PASSED
   SAS: 1234 5678 9012 3456
✅ test_verified_flag: PASSED
   Verified: 0 -> 1
✅ test_trust_store: PASSED

=== Summary ===
Passed: 4/4
```

---

## Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+
- [noise-cpp](https://github.com/ethindp/noise-cpp) (submodule)

---

## Protocol Notes

- **Handshake Pattern**: Noise XX (interactive, identity hiding for both parties)
- **Elliptic Curve**: X25519
- **Symmetric Encryption**: ChaCha20-Poly1305
- **Hash Function**: BLAKE2s
- **SAS Generation**: 4 groups of 4 digits (derived from handshake hash)

---

## Author

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

---

## License

Apache 2.0

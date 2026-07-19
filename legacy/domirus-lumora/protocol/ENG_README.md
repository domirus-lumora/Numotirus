# ZRTP Module for Numotirus

ZRTP protocol implementation based on X25519 + BLAKE2b, providing key exchange, SAS short authentication string generation, and trust storage.

---

## Features

- X25519 key exchange
- SAS (Short Authentication String) generation (4 groups of 4 digits)
- Manual SAS verification with state flag
- Trust storage (persistent public key + shared secret)
- No role dependency (automatic client/server negotiation based on lexicographic order of public keys)

---

## File Structure

```text
core/protocol/
├── zrtp.h          # Header file, public API
├── zrtp.c          # Implementation
└── test_zrtp.c     # Unit test
```

---

## Build

```bash
cd core/protocol
gcc -c zrtp.c -o zrtp.o -lsodium
```

Run unit tests:

```bash
gcc -o test_zrtp test_zrtp.c zrtp.c -lsodium
./test_zrtp
```

---

## Usage

```c
#include "core/protocol/zrtp.h"

// 1. Create session
zrtp_session_t* sess = zrtp_session_new();

// 2. Set own keypair (obtained from crypto module)
zrtp_session_set_keypair(sess, my_pubkey, my_seckey);

// 3. Set peer's public key (exchanged via P2P)
zrtp_session_set_peer_public(sess, peer_pubkey);

// 4. Perform key exchange
if (zrtp_session_key_exchange(sess) != ZRTP_SUCCESS) {
    // handle error
}

// 5. Get SAS code for user out-of-band verification
const char* sas = zrtp_session_get_sas(sess);
printf("SAS: %s\n", sas);

// 6. Mark as verified after user confirmation
zrtp_session_mark_verified(sess);

// 7. Get shared secret for encryption
const uint8_t* secret = zrtp_session_get_shared_secret(sess);

// 8. Cleanup
zrtp_session_free(sess);
```

---

## Trust Store

```c
// Save trust (call after first successful verification)
zrtp_trust_store_save("peer_id", peer_pubkey, shared_secret);

// Load trust (check on next connection)
uint8_t saved_pubkey[32], saved_secret[32];
if (zrtp_trust_store_load("peer_id", saved_pubkey, saved_secret) == ZRTP_SUCCESS) {
    // Trust exists, skip SAS verification
}
```

---

## API Reference

| Function | Description |
| ---------- | ------------- |
| `zrtp_session_new()` | Create a new session |
| `zrtp_session_free()` | Destroy session and free resources |
| `zrtp_session_set_keypair()` | Set local keypair |
| `zrtp_session_set_peer_public()` | Set peer's public key |
| `zrtp_session_key_exchange()` | Perform X25519 key exchange |
| `zrtp_session_get_sas()` | Get SAS string |
| `zrtp_session_mark_verified()` | Mark session as verified |
| `zrtp_session_is_verified()` | Check if peer is verified |
| `zrtp_session_get_shared_secret()` | Get shared secret |
| `zrtp_trust_store_save()` | Save trust entry to file |
| `zrtp_trust_store_load()` | Load trust entry from file |

---

## Test

Run unit tests to verify SAS consistency, different-keypair differentiation, verified flag, and trust storage:

```bash
gcc -o test_zrtp test_zrtp.c zrtp.c -lsodium
./test_zrtp
```

Expected output:

```bash
=== ZRTP Module Test ===

✅ test_same_sas: PASSED
   SAS: 1234 5678 9012 3456
✅ test_different_sas: PASSED
   SAS1: 1234 5678 9012 3456
   SAS2: 6543 2109 8765 4321
✅ test_verified_flag: PASSED
   Verified: 0 -> 1
✅ test_trust_store: PASSED

=== Summary ===
Passed: 4/4
```

---

## Dependencies

- [libsodium](https://doc.libsodium.org/) 1.0.18+

---

## Author

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

---

## License

Apache 2.0

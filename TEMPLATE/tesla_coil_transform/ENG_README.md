# Tesla Coil Transform (TCT)

## What it does

A three-layer reversible transform based on the EML operator (e^x - ln(w)). TCT provides obfuscation before standard encryption. Security is provided by XChaCha20-Poly1305 (AEAD), not by TCT itself. Suitable for experimental scenarios requiring an additional private obfuscation layer.

## Language

C++20

## Dependencies

- libsodium (provides XChaCha20-Poly1305)
- C++17 or later compiler
- `std::array`, `std::vector`, `std::optional`

## How to run

### Install libsodium

**MSYS2 (Windows)**:

```bash
pacman -S mingw-w64-ucrt-x86_64-libsodium
```

**Linux (Debian/Ubuntu)**:

```bash
sudo apt install libsodium-dev
```

**macOS (Homebrew)**:

```bash
brew install libsodium
```

### Build

```bash
g++ -std=c++17 tct.cpp tct_demo.cpp -lsodium -o tct_demo
```

### Run test

```bash
./tct_demo
```

## Test

The test program `tct_demo.cpp` runs 100,000 random roundtrip tests:

- Random plaintext in range [-1, 1]
- Random 24-byte nonce
- Encrypt → Decrypt
- Reports success rate, max error, average error

## How to verify it works

1. Build and run `tct_demo`
2. Expected output:
   - `Valid roundtrips: 100000 / 100000` (all succeed)
   - `Failures (NaN/overflow): 0` (no numerical errors)
   - `Max error: < 1e-5` (error within one part per million)

## Example

```cpp
#include "tct.hpp"

int main() {
    // Shared secrets
    uint64_t secret = 0x123456789ABCDEF0ULL;
    std::array<uint8_t, 32> key{};
    randombytes_buf(key.data(), key.size());

    tct::TeslaCoilTransform tct(secret, key);

    // Original byte (0-255)
    uint8_t original = 42;
    int64_t max_val = 255;

    // Scale to [-1, 1]
    double plain = tct.scale_to_plaintext(original, max_val);

    // Random nonce
    tct::Nonce nonce{};
    randombytes_buf(nonce.data(), nonce.size());

    // Encrypt (TCT + XChaCha20-Poly1305)
    auto ciphertext = tct.encrypt(plain, nonce);

    // Decrypt
    auto decrypted = tct.decrypt(ciphertext);

    if (decrypted) {
        int64_t recovered = tct.scale_from_plaintext(*decrypted, max_val);
        // recovered == original
    }

    return 0;
}
```

## Input / Output

- **Input (plaintext)**：`double` in range [-1, 1] (use `scale_to_plaintext` to normalize integers)
- **Output (ciphertext)**：`std::vector<uint8_t>` formatted as `[nonce(24 bytes)] + [XChaCha20-Poly1305 ciphertext]`
- **Keys**：`uint64_t secret` (obfuscation layer) + `std::array<uint8_t, 32> key` (encryption key)

## Pre-built binary (test only)

A Windows executable `tct_demo.exe` is included for quick testing, compiled by the module author.

**Note**: This binary is for testing only. For production use, please compile from source.

## Citation

Thanks for the [EML paper](https://arxiv.org/html/2603.21852v2)'s author discover about EML.

## Author

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

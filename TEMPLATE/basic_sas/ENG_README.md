# ZRTP Basic SAS

## What it does

Derives 4 groups of 4-digit numbers from a shared secret for voice/off-band comparison to prevent MITM attacks in P2P communication.

## Language

C99

## Dependencies

- libsodium 1.0.18+

## How to run

```bash
gcc -c sas.c -o sas.o -lsodium
```

## Test

```bash
gcc -o test_sas test_sas.c sas.c -lsodium
./test_sas
```

## How to verify it works

1. Run the test program with the same shared secret (e.g., 32 random bytes)
2. Both runs should produce the identical SAS code
3. Changing any single bit of the shared secret should produce a completely different SAS code

## Example

```c
#include "sas.h"
#include <sodium.h>
#include <stdio.h>

int main() {
    if (sodium_init() < 0) return 1;
    
    // Simulate shared secret (from X25519 key exchange)
    uint8_t shared_secret[32];
    randombytes_buf(shared_secret, 32);
    
    char sas[20];
    if (sas_generate(shared_secret, 32, sas, sizeof(sas)) == 0) {
        printf("ZRTP SAS: %s\n", sas);
    }
    return 0;
}
```

## Input / Output

- Input: 32-byte shared secret (`const uint8_t*`)
- Output: 20-byte string, formatted as `"1234 5678 9012 3456"` (including trailing `\0`)

## Author

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

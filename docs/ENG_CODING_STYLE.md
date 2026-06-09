# C11 / C++20 Coding Style Guide

Thank you for following these rules. They are not here to annoy you, but to make code readable, correct, and bug-free for everyone.

This guide covers **C11** (pure C modules like `crypto_c.c`, `p2p.c`) and **C++20** (core protocol layer).

---

## Part 1: General Rules (C11 + C++20)

### 1. Naming Rules

| Type | Style | C Example | C++ Example |
| ------ | ------- | ----------- | ------------- |
| Variables | `snake_case` | `int peer_id;` | `int peer_id;` |
| Functions | `snake_case` (C) / `PascalCase` (C++) | `void encrypt_message();` | `void EncryptMessage();` |
| Structs/Classes | `PascalCase` | `struct P2PNode {};` | `class KeyExchange {};` |
| Constants | `kPascalCase` or `UPPER_CASE` | `const int K_MAX_RETRY = 3;` | `const int kMaxRetry = 3;` |
| Private members | trailing `_` | — | `int peer_id_;` |
| Enum classes | `PascalCase` | `enum ErrorCode` | `enum class ErrorCode` |
| Enum values | `kPascalCase` | `ERROR_INVALID_ARG` | `ErrorCode::kInvalidArgument` |

```c
// Correct C example
int peer_id;
struct P2PNode* node;
const int K_MAX_RETRY = 3;
enum ErrorCode {
    ERROR_SUCCESS = 0,
    ERROR_INVALID_ARG = -1
};
```

```cpp
// Correct C++ example
int peer_id;
class KeyExchange {};
const int kMaxRetry = 3;
enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument = -1
};
```

### 2. Comment Standards

Bilingual: English first, then Chinese.

```c
// Encrypt message with X25519. 使用 X25519 加密消息。
void encrypt(const uint8_t* input, size_t len);
```

```cpp
// Returns decrypted data on success. 成功时返回解密数据。
Result<std::vector<uint8_t>> Decrypt(std::span<const uint8_t> ciphertext);
```

**Public interfaces MUST have comments** (function purpose, parameters, return value).

### 3. Formatting

Use `clang-format` with `.clang-format`:

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
```

Run before commit:

```bash
clang-format -i core/*.c core/*.cpp core/*.h
```

### 4. Header File Standards

```c
// C uses include guard
#ifndef P2P_H
#define P2P_H
// ...
#endif
```

```cpp
// C++ can use #pragma once
#pragma once
```

### 5. #include Order

```c
// 1. Corresponding header
#include "p2p.h"

// 2. C standard library
#include <stdio.h>
#include <string.h>

// 3. Third-party libraries
#include <sodium.h>

// 4. Project headers
#include "crypto_c.h"
```

---

## Part 2: C11 Specific Rules (`core/crypto/`, `core/p2p/`)

### 1. Allowed C Features

| Feature | Allowed | Notes |
| --------- | --------- | ------- |
| Raw pointers | ✅ | C has no RAII, document ownership with comments |
| `malloc`/`free` | ✅ | Must be paired, avoid leaks |
| C-style arrays | ✅ | Fixed size, or use `sizeof` |
| Function pointers | ✅ | Required for callbacks |
| `struct` | ✅ | Data encapsulation |
| `const` | ✅ | Read-only parameters |

### 2. Memory Management

```c
// Correct: allocation + free paired
CryptoKeypair* kp = crypto_keypair_generate();
// ... use kp
crypto_keypair_free(kp);

// Correct: output parameter + caller frees
uint8_t* cipher = NULL;
size_t clen = 0;
crypto_encrypt_public(plain, len, pubkey, &cipher, &clen);
// ... use cipher
free(cipher);
```

**Rules**:

- Who allocates, who frees
- Output parameters like `uint8_t** out` must document that caller must `free()`
- Prefer `calloc` over `malloc + memset`

### 3. Error Handling

```c
// C style: return error code, data via output parameters
int crypto_encrypt_public(const uint8_t* plain, size_t plain_len,
                          const uint8_t* pubkey,
                          uint8_t** out, size_t* out_len) {
    if (!plain || !pubkey || !out) return -1;
    // ...
    return 0;  // success
}
```

**Error code convention**:

- `0` = success
- `-1` = invalid argument
- `-2` = memory allocation failed
- `-3` = encryption/decryption failed

### 4. Cross-Platform Macros

```c
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
typedef SOCKET socket_t;
#define CLOSE closesocket
#else
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE close
#endif
```

**Rules**:

- Platform differences must be isolated with `#ifdef`
- Provide unified abstractions (e.g., `socket_t`, `CLOSE`)

### 5. Thread Safety (C11 has no standard library)

```c
// C uses platform-native synchronization primitives
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    EnterCriticalSection(&mutex);
    LeaveCriticalSection(&mutex);
#else
    pthread_mutex_t mutex;
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
#endif
```

---

## Part 3: C++20 Specific Rules (`core/protocol/`, future modules)

### 1. Naming Rules (C++ part)

| Type | Style | Example |
| ------ | ------- | --------- |
| Variables | `snake_case` | `int peer_id;` |
| Classes | `PascalCase` | `class KeyExchange {};` |
| Functions | `PascalCase` | `void EncryptMessage();` |
| Constants | `kPascalCase` | `const int kMaxRetry = 3;` |
| Private members | trailing `_` | `int peer_id_;` |
| Enum classes | `PascalCase` | `enum class ErrorCode {}` |
| Enum values | `kPascalCase` | `ErrorCode::kInvalidArgument` |

### 2. Prohibited Items

| Prohibited | Replacement |
| ------------ | ------------- |
| Raw pointers (owning) | `std::unique_ptr`, `std::shared_ptr` |
| `NULL` | `nullptr` |
| C-style casts | `static_cast`, `dynamic_cast`, `const_cast` |
| Global variables | `constexpr` constants |
| Manual `new`/`delete` | RAII containers |
| C-style arrays | `std::array`, `std::vector`, `std::span` |
| Macros (constants/functions) | `constexpr`, `consteval`, templates |
| Raw `enum` | `enum class` |

### 3. Error Handling (`tl::expected`)

```cpp
#include <tl/expected.hpp>

enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument,
    kNetworkError,
};

template<typename T>
using Result = tl::expected<T, ErrorCode>;

Result<std::vector<uint8_t>> Decrypt(std::span<const uint8_t> ciphertext) {
    if (ciphertext.empty()) {
        return tl::unexpected(ErrorCode::kInvalidArgument);
    }
    return decrypted_data;
}
```

### 4. RAII Principle

```cpp
// Correct: resources managed by objects
class GoodExample {
    std::vector<uint8_t> buffer_;
public:
    GoodExample() : buffer_(1024) {}
    // No destructor needed, automatic cleanup
};
```

### 5. Smart Pointer Selection

| Scenario | Choice |
| ---------- | -------- |
| Exclusive ownership | `std::unique_ptr<T>` |
| Shared ownership | `std::shared_ptr<T>` (use sparingly) |
| Observing (non-owning) | Raw pointer `T*` or reference `T&` |

### 6. Thread Safety

```cpp
#include <mutex>

class ThreadSafeCounter {
    mutable std::mutex mutex_;
    int value_ = 0;
public:
    void Increment() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++value_;
    }
};
```

### 7. C++20 Core Features

#### std::span

```cpp
void SendPacket(std::span<const uint8_t> payload) {
    // payload.data(), payload.size()
}
```

#### std::string_view

```cpp
void Log(std::string_view msg) {}  // replaces const std::string&
```

#### constexpr

```cpp
constexpr size_t kMaxPacketSize = 65535;
constexpr uint32_t Crc32(const char* str) { /* compile-time computation */ }
```

### 8. AI and Translation Tool Usage

- **DeepL**: Prefer for translating comments/docs, must manually review after translation
- **AI code generation**: Allowed, but must understand every line, test yourself, declare in PR
- **Placeholder**: `// * L10N_PENDING [YYYYMMDD:HHMMSS] *`

---

## Part 4: C/C++ Mixed Boundaries (`extern "C"`)

### 1. Header File Pattern

```cpp
// crypto_c.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CryptoKeypair CryptoKeypair;

CryptoKeypair* crypto_keypair_generate(void);
void crypto_keypair_free(CryptoKeypair* kp);

#ifdef __cplusplus
}
#endif
```

### 2. C++ Calling C

```cpp
#include "crypto_c.h"

// C functions are already declared as extern "C" in crypto_c.h
auto kp = crypto_keypair_generate();  // call directly
```

### 3. Memory Management Rules

| Allocated by | Freed by | Scenario |
| -------------- | ---------- | ---------- |
| C (`malloc`) | C (`free`) | C API returns data |
| C++ (`new`) | C++ (`delete`) | C++ internal objects |
| C++ (`std::make_unique`) | Automatic | RAII management |

**Forbidden**: Allocate in C, free in C++ (or vice versa)

---

## Part 5: AI and Translation Tool Usage

This project allows the use of AI and translation tools, but **the following rules MUST be followed**.

### 1. Translation Tools (DeepL)

When translating comments or documentation, **prioritize DeepL**:

1. Write in English first, translate to Chinese with DeepL
2. Or write in Chinese first, translate to English with DeepL
3. **Must manually review** after translation to ensure terminology accuracy and appropriate tone

```cpp
// Encrypt message with X25519. 使用 X25519 加密消息。
void Encrypt(const uint8_t* input, size_t len);
```

### 2. AI Code Generation

AI (Claude, ChatGPT, etc.) may be used to generate code, but you MUST:

| Rule | Description |
| ------ | ------------- |
| **Understand every line** | Don't submit code you don't understand |
| **Test it yourself** | AI-generated code may have bugs |
| **Declare usage** | Write "Partially generated by AI" in PR description |
| **Follow this guide** | AI-generated code must still pass clang-format and review |

### 3. Prohibited Behaviors

- ❌ Copy-pasting AI-generated code without understanding
- ❌ Using AI to generate comments without checking accuracy
- ❌ Using AI to generate test cases without verification

### 4. Placeholder

If DeepL cannot translate (or the translation is clearly wrong), use a placeholder:

```cpp
void SomeFunction(); // * L10N_PENDING [DOMIRUS-20260613:235901] *
```

Format: `* L10N_PENDING [author-YYYYMMDD:HHMMSS] *`

### 5. Why These Rules

Not because AI is bad.  
Because **you must be the master of the code. AI is just a tool**.

- You read the ZRTP RFC, AI helps write the framework → ✅
- You don't understand RSA, ask AI to write encryption → ❌
- You write bilingual comments, DeepL assists translation → ✅
- You let AI translate without checking → ❌

**Understand every line. Test yourself. Declare usage.**

---

## Appendix: Quick Reference Card

### C Quick Reference

| What to do | How to write |
| ------------ | -------------- |
| Allocate memory | `calloc(1, sizeof(T))` |
| Free memory | `free(ptr)` |
| Return error | `int` error code + output parameters |
| Cross-platform | `#ifdef _WIN32` |
| Thread lock | `CRITICAL_SECTION` / `pthread_mutex_t` |

### C++ Quick Reference

| What to do | How to write |
| ------------ | -------------- |
| Exclusive pointer | `auto p = std::make_unique<T>()` |
| Read-only parameter | `std::span<const T>` or `std::string_view` |
| Return error | `Result<T>` (`tl::expected`) |
| Compile-time constant | `constexpr` |
| Array view | `std::span<T>` |
| Thread lock | `std::lock_guard<std::mutex>` |

---

**Document Version**: 2.0  
**Language Standards**: C11 + C++20  
**Last Updated**: 2026-06-09

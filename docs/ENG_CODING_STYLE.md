# C++20 Coding Style Guide

Thank you for following these rules. They are not here to annoy you, but to make code readable, correct, and bug-free for everyone.

## 1. Naming Rules

| Type | Style | Example |
| ------ | ------- | --------- |
| Variables | `snake_case` | `int peer_id;` |
| Classes | `PascalCase` | `class KeyExchange {};` |
| Functions | `PascalCase` | `void EncryptMessage();` |
| Constants | `kPascalCase` or `CONSTANT_CASE` | `const int kMaxRetry = 3;` |
| Private members | trailing `_` | `int peer_id_;` |
| Namespaces | `snake_case` | `namespace crypto_utils {}` |
| Enum classes | `PascalCase` | `enum class ErrorCode {}` |
| Enum values | `kPascalCase` | `ErrorCode::kInvalidArgument` |
| Template parameters | `T` or `PascalCase` | `template<typename T>` |

```cpp
int peer_id;           // OK
int peerID;            // NOT OK
int m_peerId;          // NOT OK

class KeyExchange {};  // OK
class key_exchange {}; // NOT OK

void EncryptMessage(); // OK
void encrypt_message();// NOT OK

const int kMaxRetry = 3;   // OK
const int MAX_RETRY = 3;   // OK
```

---

## 2. Prohibited Items

| Prohibited | Replacement |
| ------------ | ------------- |
| Raw pointers (owning) | `std::unique_ptr`, `std::shared_ptr` |
| `NULL` | `nullptr` |
| C-style casts | `static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast` |
| Global variables | `constexpr` constants, singleton (use sparingly) |
| Manual `new`/`delete` | RAII containers |
| C-style arrays | `std::array`, `std::vector`, `std::span` |
| Macros (constants/functions) | `constexpr`, `consteval`, templates |
| Raw `enum` | `enum class` |

```cpp
// NOT OK
int* p = new int;
if (ptr == NULL) {}
int x = (int)y;
int arr[100];

// OK
auto p = std::make_unique<int>();
if (ptr == nullptr) {}
int x = static_cast<int>(y);
std::array<int, 100> arr;
```

---

## 3. Header File Standards

```cpp
// Use #pragma once (concise, supported by all major compilers)
#pragma once

// Or use include guard (traditional)
#ifndef MODULE_NAME_H_
#define MODULE_NAME_H_
// ...
#endif  // MODULE_NAME_H_
```

**Choice**: Use `#pragma once` for new projects. Use include guards when maximum portability is required.

---

## 4. #include Order

```cpp
// 1. Corresponding header (in .cpp files)
#include "my_class.h"

// 2. C standard library
#include <cstddef>
#include <cstring>

// 3. C++ standard library
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 4. Third-party libraries (alphabetical)
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <tl/expected.hpp>

// 5. Project headers (alphabetical)
#include "base/utils.h"
#include "network/peer.h"
```

Add a blank line between each group.

---

## 5. Comment Standards

Bilingual: English first, then Chinese.

```cpp
// Encrypt message with X25519. 使用 X25519 加密消息。
void Encrypt(const std::string& input);

// Returns decrypted data on success, error code on failure.
// 成功时返回解密数据，失败时返回错误码。
Result<std::vector<uint8_t>> Decrypt(std::span<const uint8_t> ciphertext);
```

**Public interfaces MUST have comments** explaining:

- What the function does
- Parameter meanings
- Return value meaning
- Possible exceptions or error codes

**Placeholder** (if you don't know how to write a comment in English or Chinese):

```cpp
void SomeFunction(); // * L10N_PENDING [YYYYMMDD:HHMMSS] *
```

---

## 6. Formatting

Use `clang-format` with `.clang-format`:

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
Language: Cpp
Standard: c++20
```

Run before commit:

```bash
clang-format -i src/*.cpp src/*.h
```

---

## 7. Error Handling

**C++20 does not have `std::expected` (it arrives in C++23). This project uses `tl::expected`.**

### Installing tl::expected

```bash
# vcpkg
vcpkg install tl-expected

# Conan
conan install tl-expected/1.1.0

# Or download the header directly
# https://github.com/TartanLlama/expected
```

### Usage Example

```cpp
#include <tl/expected.hpp>

using tl::expected;

// Error code definition
enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument,
    kNetworkError,
    kDecryptionFailed,
};

// Result type
template<typename T>
using Result = expected<T, ErrorCode>;

// Usage example
Result<std::vector<uint8_t>> DecryptMessage(
    std::span<const uint8_t> ciphertext,
    const std::array<uint8_t, 32>& private_key) {
    
    if (ciphertext.empty()) {
        return std::unexpected(ErrorCode::kInvalidArgument);
    }
    
    // Decryption logic...
    return decrypted_data;
}

// Caller handling
auto result = DecryptMessage(data, key);
if (result.has_value()) {
    Process(result.value());
} else {
    LogError("Decrypt failed: ", static_cast<int>(result.error()));
}
```

### Fallback Options

Use only when `tl::expected` is unavailable:

```cpp
// Option 2: optional + separate error code
struct DecryptResult {
    std::optional<std::vector<uint8_t>> data;
    ErrorCode error = ErrorCode::kSuccess;
};

// Option 3: Exceptions (only in constructors, operator overloads)
std::vector<uint8_t> DecryptOrThrow(std::span<const uint8_t> data) {
    if (data.empty()) throw std::invalid_argument("empty data");
    // ...
}
```

---

## 8. RAII Principle

**All resources must be managed by objects. Manual `new`/`delete` is forbidden.**

```cpp
// NOT OK: manual management
class BadExample {
    uint8_t* buffer_;
public:
    BadExample() : buffer_(new uint8_t[1024]) {}
    ~BadExample() { delete[] buffer_; }  // Easy to forget, not exception-safe
};

// OK: RAII
class GoodExample {
    std::vector<uint8_t> buffer_;
public:
    GoodExample() : buffer_(1024) {}  // Automatically managed
    // No destructor needed, automatic cleanup
};
```

---

## 9. Smart Pointer Selection

| Scenario | Choice |
| ---------- | -------- |
| Exclusive ownership | `std::unique_ptr<T>` |
| Shared ownership | `std::shared_ptr<T>` (use sparingly) |
| Weak reference | `std::weak_ptr<T>` |
| Factory return | `std::unique_ptr<T>` |
| Observing (non-owning) | Raw pointer `T*` or reference `T&` |

```cpp
// Exclusive ownership example
class Connection {
    std::unique_ptr<Socket> socket_;
public:
    Connection() : socket_(std::make_unique<Socket>()) {}
    // Movable, not copyable
    Connection(Connection&&) = default;
};

// Factory function
std::unique_ptr<KeyPair> CreateKeyPair() {
    return std::make_unique<KeyPair>();
}

// Observer pattern (non-owning)
void ProcessData(const Data& data, Logger* logger) {
    if (logger) {
        logger->Log("Processing");
    }
}
```

---

## 10. C++20 Core Features

### std::span (replaces raw array pointers)

```cpp
// Preferred for parameter passing
void SendPacket(std::span<const uint8_t> payload) {
    // payload.data(), payload.size()
}

// Construct from different containers
std::vector<uint8_t> vec = {1,2,3};
std::array<uint8_t, 3> arr = {1,2,3};
uint8_t c_arr[] = {1,2,3};

SendPacket(vec);      // OK
SendPacket(arr);      // OK
SendPacket(c_arr);    // OK

// Dynamic slicing
void ProcessHeader(std::span<uint8_t> buffer, size_t offset) {
    auto header = buffer.subspan(offset, 64);
    // Caller must ensure offset+64 <= buffer.size()
}
```

### std::string_view (replaces const std::string&)

```cpp
// Old way
void Log(const std::string& msg) {}

// New way (more efficient)
void Log(std::string_view msg) {}

// Usage
Log("hello");           // OK, no temporary string created
Log(std::string("hi")); // OK
```

### Concepts

```cpp
// Define a concept
template<typename T>
concept ByteContainer = requires(T t) {
    { t.data() } -> std::convertible_to<const uint8_t*>;
    { t.size() } -> std::convertible_to<size_t>;
};

// Use concept (concise version)
void ProcessBytes(ByteContainer auto&& container) {
    auto span = std::span(container.data(), container.size());
    // Process...
}

// Use concept (full version)
template<ByteContainer T>
void ProcessBytes(T&& container);
```

### consteval (forced compile-time execution)

```cpp
// Must execute at compile time
consteval uint32_t CompileTimeHash(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash = (hash ^ *str++) * 16777619u;
    }
    return hash;
}

// Compile-time verification
static_assert(CompileTimeHash("test") == 0x3c4e2a6b);

// Error: cannot be called at runtime
// uint32_t runtime = CompileTimeHash(getenv("str"));  // Compilation error
```

### Coroutines (use when needed)

```cpp
#include <coroutine>

// Simple coroutine task
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

// Async operation example
Task AsyncRead(Socket& sock, std::span<uint8_t> buffer) {
    co_await sock.async_read_some(buffer);
    // Continue after co_await
}
```

**Coroutine usage principles**:

- Use only when async I/O is needed
- Must have proper RAII management
- Be aware of coroutine stack frame lifetimes

---

## 11. Thread Safety

```cpp
#include <mutex>
#include <shared_mutex>

class ThreadSafeCounter {
private:
    mutable std::mutex mutex_;  // mutable allows const methods to lock
    int value_ = 0;
    
public:
    void Increment() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++value_;
    }
    
    int Get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }
};

// Use shared_mutex for read-heavy scenarios
class ThreadSafeCache {
private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::vector<uint8_t>> cache_;
    
public:
    std::optional<std::vector<uint8_t>> Get(std::string_view key) const {
        std::shared_lock lock(mutex_);  // Shared lock
        auto it = cache_.find(std::string(key));
        if (it != cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    void Set(std::string key, std::vector<uint8_t> value) {
        std::unique_lock lock(mutex_);  // Exclusive lock
        cache_[std::move(key)] = std::move(value);
    }
};
```

**Lock selection guide**:

| Scenario | Recommendation |
| ---------- | ---------------- |
| General | `std::mutex` + `std::lock_guard` |
| Read-heavy | `std::shared_mutex` + `std::shared_lock` |
| Recursive needed | `std::recursive_mutex` (avoid if possible) |
| Timed try | `std::timed_mutex` |

---

## 12. constexpr and consteval

```cpp
// Compile-time constants
constexpr size_t kMaxPacketSize = 65535;
constexpr double kPi = 3.141592653589793;

// Compile-time functions (C++20 supports more features)
constexpr uint32_t Crc32(const char* str) {
    uint32_t crc = 0xFFFFFFFF;
    for (; *str; ++str) {
        crc = (crc >> 8) ^ kCrcTable[(crc ^ *str) & 0xFF];
    }
    return ~crc;
}

static_assert(Crc32("test") == 0xD87F7E0C);

// C++20: constexpr supports dynamic allocation
class constexpr_string {
    char* data_;
    size_t size_;
public:
    constexpr constexpr_string(const char* str) 
        : data_(new char[std::strlen(str) + 1])
        , size_(std::strlen(str)) 
    {
        std::copy(str, str + size_ + 1, data_);
    }
    
    constexpr ~constexpr_string() { delete[] data_; }
    constexpr size_t size() const { return size_; }
};

// constinit: guaranteed compile-time initialization (avoid static init order issues)
constinit std::array<int, 3> kValues = {1, 2, 3};
```

---

## 13. AI and Translation Tool Usage

### DeepL Usage Rules

When translating comments or documentation, **prioritize DeepL**:

1. Write in English first, translate to Chinese with DeepL
2. Or write in Chinese first, translate to English with DeepL
3. **Must manually review** after translation to ensure terminology accuracy and appropriate tone

### AI Code Generation Rules

AI (Copilot, ChatGPT, etc.) may be used to generate code, but you MUST:

1. **Understand every line**: Don't submit code you don't understand
2. **Test it yourself**: AI-generated code may have bugs
3. **Declare usage**: Write "Partially generated by AI" in PR description
4. **Follow this guide**: AI-generated code must still pass clang-format and review

### Placeholder

If DeepL cannot translate (or the translation is clearly wrong), use a placeholder:

```cpp
void SomeFunction(); // * L10N_PENDING [DOMIRUS-20260613:235901] *
```

Timestamp uses the moment you write the code. Format: `YYYYMMDD:HHMMSS` (24-hour).

Keep the placeholder format fixed. Do not change it. Someone will fill it later.

---

## 14. Testing Standards

### File Structure

```text
test/
├── encryption_test.cpp
├── network_test.cpp
└── utils_test.cpp
```

### Test Code Example

```cpp
// test/encryption_test.cpp
#include <gtest/gtest.h>
#include "encryption.h"

class EncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Executed before each test
        key_ = GenerateTestKey();
    }
    
    void TearDown() override {
        // Executed after each test (optional)
    }
    
    std::array<uint8_t, 32> key_;
};

// Test naming: FunctionName_Scenario_ExpectedBehavior
TEST_F(EncryptionTest, EncryptThenDecrypt_ValidInput_ReturnsOriginal) {
    std::string plaintext = "Hello, World!";
    auto cipher = Encrypt(plaintext, key_);
    ASSERT_TRUE(cipher.has_value());
    
    auto decrypted = Decrypt(cipher.value(), key_);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), plaintext);
}

TEST_F(EncryptionTest, Encrypt_EmptyInput_ReturnsError) {
    auto result = Encrypt("", key_);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::kInvalidArgument);
}

TEST_F(EncryptionTest, Decrypt_InvalidKey_ReturnsError) {
    std::array<uint8_t, 32> wrong_key = {};
    auto result = Decrypt(some_ciphertext, wrong_key);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::kDecryptionFailed);
}
```

### Test Coverage Requirements

| Module Type | Line Coverage | Branch Coverage |
| ------------- | --------------- | ----------------- |
| Core encryption/decryption | ≥ 90% | ≥ 80% |
| Network I/O | ≥ 80% | ≥ 70% |
| Utilities | ≥ 85% | ≥ 75% |

---

## 15. Review Checklist

Check each item before submitting code:

### Style

- [ ] Variable names `snake_case`
- [ ] Class/function names `PascalCase`
- [ ] Constants `kPascalCase`
- [ ] Private members trailing `_`
- [ ] Pass `clang-format`

### Prohibited Items

- [ ] No raw owning pointers
- [ ] No `NULL` (use `nullptr`)
- [ ] No C-style casts
- [ ] No global variables (`constexpr` exempt)
- [ ] No manual `new`/`delete`

### Memory & Resources

- [ ] All resources use RAII
- [ ] Smart pointer selection is correct
- [ ] No memory leaks
- [ ] No dangling references/pointers

### Concurrency

- [ ] Shared data is lock-protected
- [ ] No data races
- [ ] Lock granularity is appropriate

### Error Handling

- [ ] Use `tl::expected` for errors
- [ ] All `Result`s are checked
- [ ] Error codes are meaningful

### Comments & Documentation

- [ ] Public interfaces have bilingual comments
- [ ] Comments explain functionality, parameters, return values
- [ ] Complex logic has explanatory comments

### Testing

- [ ] New features have unit tests
- [ ] Happy path tests pass
- [ ] Error path tests pass
- [ ] Edge cases are tested

### Other

- [ ] `#include` order is correct
- [ ] Use C++20 features (no legacy patterns)

---

## 16. Markdown Standards

All `.md` files in this project must pass `markdownlint` checks.

### Required Rules

| Rule | Description |
| ------ | ------------- |
| MD001 | Heading levels must increment by one |
| MD003 | Consistent heading style, use `#` prefix |
| MD004 | Unordered lists use `-` |
| MD009 | No trailing spaces |
| MD010 | No tabs, use spaces (4 spaces) |
| MD012 | No multiple consecutive blank lines (max 1) |
| MD022 | Blank lines around headings |
| MD031 | Blank lines around fenced code blocks |
| MD032 | Blank lines around lists |
| MD035 | Horizontal rules use `---` |
| MD040 | Fenced code blocks must specify language |
| MD041 | First line should be top-level heading |
| MD047 | Files end with single newline |

### Recommended Rules

| Rule | Description |
| ------ | ------------- |
| MD013 | Line length ≤ 120 characters |
| MD024 | Avoid duplicate heading content |
| MD033 | Avoid inline HTML |
| MD034 | Use `[text](URL)` instead of bare URLs |
| MD036 | Emphasis used instead of heading |

### Check Commands

```bash
# Check all md files
markdownlint *.md docs/*.md

# Auto-fix some issues
markdownlint --fix *.md
```

**VS Code users**: Install `markdownlint` extension for automatic checking.

---

## Appendix A: Common Code Templates

### Class Template

```cpp
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace my_namespace {

// ClassName does something. ClassName 做某事。
class ClassName {
public:
    // Constructs a new ClassName. 构造新 ClassName。
    explicit ClassName(std::span<const uint8_t> config);
    
    // Move constructor. 移动构造函数。
    ClassName(ClassName&&) = default;
    
    // Move assignment. 移动赋值运算符。
    ClassName& operator=(ClassName&&) = default;
    
    // Copy is disabled. 禁用拷贝。
    ClassName(const ClassName&) = delete;
    ClassName& operator=(const ClassName&) = delete;
    
    // Processes input and returns result. 处理输入并返回结果。
    Result<std::vector<uint8_t>> Process(std::span<const uint8_t> input);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;  // Pimpl idiom
};

}  // namespace my_namespace
```

### Function Template

```cpp
// Serializes any byte container. 序列化任何字节容器。
template<ByteContainer Container>
Result<std::vector<uint8_t>> Serialize(const Container& data) {
    std::vector<uint8_t> result;
    result.reserve(data.size());
    result.insert(result.end(), data.begin(), data.end());
    return result;
}
```

---

## Appendix B: Quick Reference Card

| What to do | How to write |
| ------------ | -------------- |
| Exclusive pointer | `auto p = std::make_unique<T>(args...)` |
| Read-only parameter | `std::span<const T>` or `std::string_view` |
| Return error | `Result<T>` (`tl::expected`) |
| Compile-time constant | `constexpr` |
| Forced compile-time | `consteval` |
| Array view | `std::span<T>` |
| Read-only string | `std::string_view` |
| Thread-safe lock | `std::lock_guard<std::mutex>` |
| Read-heavy lock | `std::shared_lock` / `std::unique_lock` |
| Disable copy | `= delete` |
| Move semantics | `= default` |

---

**Document Version**: 1.0  
**C++ Standard**: C++20  
**Last Updated**: 2026-06-02

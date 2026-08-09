# C11 / C++20 代码规范指南

感谢你遵守这些规则。不是为了刁难你，是为了让代码能被所有人看懂、改对、不出 bug。

本规范覆盖 **C11**（纯 C 模块，如 `crypto_c.c`、`p2p.c`）和 **C++20**（核心协议层）。

---

## 第一部分：通用规则（C11 + C++20）

### 1. 命名规则

| 类型 | 风格 | C 示例 | C++ 示例 |
| ------ | ------ | -------- | ---------- |
| 变量 | `snake_case` | `int peer_id;` | `int peer_id;` |
| 函数 | `snake_case` (C) / `PascalCase` (C++) | `void encrypt_message();` | `void EncryptMessage();` |
| 结构体/类 | `PascalCase` | `struct P2PNode {};` | `class KeyExchange {};` |
| 常量 | `kPascalCase` 或 `UPPER_CASE` | `const int K_MAX_RETRY = 3;` | `const int kMaxRetry = 3;` |
| 私有成员 | 末尾加 `_` | — | `int peer_id_;` |
| 枚举类 | `PascalCase` | `enum ErrorCode` | `enum class ErrorCode` |
| 枚举值 | `kPascalCase` | `ERROR_INVALID_ARG` | `ErrorCode::kInvalidArgument` |

```c
// C 正确示例
int peer_id;
struct P2PNode* node;
const int K_MAX_RETRY = 3;
enum ErrorCode {
    ERROR_SUCCESS = 0,
    ERROR_INVALID_ARG = -1
};
```

```cpp
// C++ 正确示例
int peer_id;
class KeyExchange {};
const int kMaxRetry = 3;
enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument = -1
};
```

### 2. 注释规范

中英双语，英文在前：

```c
// Encrypt message with X25519. 使用 X25519 加密消息。
void encrypt(const uint8_t* input, size_t len);
```

```cpp
// Returns decrypted data on success.
//成功时返回解密数据。

Result<std::vector<uint8_t>> Decrypt(std::span<const uint8_t> ciphertext);
```

**公共接口必须注释**（函数作用、参数、返回值）。

### 3. 格式化

使用 `clang-format`，配置文件 `.clang-format`：

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
```

提交前运行：

```bash
clang-format -i core/*.c core/*.cpp core/*.h
```

### 4. 头文件规范

```c
// C 使用 include guard
#ifndef P2P_H
#define P2P_H
// ...
#endif
```

```cpp
// C++ 可用 #pragma once
#pragma once
```

### 5. #include 顺序

```c
// 1. 当前文件对应的头文件
#include "p2p.h"

// 2. C 标准库
#include <stdio.h>
#include <string.h>

// 3. 第三方库
#include <sodium.h>

// 4. 项目内部头文件
#include "crypto_c.h"
```

---

## 第二部分：C11 专项规则（`core/crypto/`、`core/p2p/`）

### 1. 允许的 C 特性

| 特性 | 允许 | 说明 |
| ------ | ------ | ------ |
| 裸指针 | ✅ | C 没有 RAII，所有权用注释标明 |
| `malloc`/`free` | ✅ | 必须配对，避免泄漏 |
| C 风格数组 | ✅ | 固定大小，或配合 `sizeof` |
| 函数指针 | ✅ | 回调机制必需 |
| `struct` | ✅ | 数据封装 |
| `const` | ✅ | 只读参数 |

### 2. 内存管理

```c
// 正确：分配 + 释放配对
CryptoKeypair* kp = crypto_keypair_generate();
// ... 使用 kp
crypto_keypair_free(kp);

// 正确：输出参数 + 调用者释放
uint8_t* cipher = NULL;
size_t clen = 0;
crypto_encrypt_public(plain, len, pubkey, &cipher, &clen);
// ... 使用 cipher
free(cipher);
```

**规则**：

- 谁分配，谁释放
- 输出参数如 `uint8_t** out`，文档必须说明调用者需 `free`
- 使用 `calloc` 替代 `malloc + memset`

### 3. 错误处理

```c
// C 方式：返回错误码，数据通过输出参数返回
int crypto_encrypt_public(const uint8_t* plain, size_t plain_len,
                          const uint8_t* pubkey,
                          uint8_t** out, size_t* out_len) {
    if (!plain || !pubkey || !out) return -1;
    // ...
    return 0;  // 成功
}
```

**错误码约定**：

- `0` = 成功
- `-1` = 参数错误
- `-2` = 内存分配失败
- `-3` = 加密/解密失败

### 4. 跨平台宏

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

**规则**：

- 平台差异必须用 `#ifdef` 隔离
- 提供统一抽象（如 `socket_t`、`CLOSE`）

### 5. 线程安全（C11 无标准库）

```c
// C 使用平台原生同步原语
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

## 第三部分：C++20 专项规则（`core/protocol/`、未来模块）

### 1. 命名规则（C++ 部分）

| 类型 | 风格 | 示例 |
| ------ | ------ | ------ |
| 变量 | `snake_case` | `int peer_id;` |
| 类名 | `PascalCase` | `class KeyExchange {};` |
| 函数名 | `PascalCase` | `void EncryptMessage();` |
| 常量 | `kPascalCase` | `const int kMaxRetry = 3;` |
| 私有成员 | 末尾加 `_` | `int peer_id_;` |
| 枚举类 | `PascalCase` | `enum class ErrorCode {}` |
| 枚举值 | `kPascalCase` | `ErrorCode::kInvalidArgument` |

### 2. 禁止项

| 禁止 | 替代 |
| ------ | ------ |
| 裸指针（拥有所有权） | `std::unique_ptr`、`std::shared_ptr` |
| `NULL` | `nullptr` |
| C 风格转换 | `static_cast`、`dynamic_cast`、`const_cast` |
| 全局变量 | `constexpr` 常量 |
| 手动 `new`/`delete` | RAII 容器 |
| C 风格数组 | `std::array`、`std::vector`、`std::span` |
| 宏（常量/函数） | `constexpr`、`consteval`、模板 |
| 裸 `enum` | `enum class` |

### 3. 错误处理（`tl::expected`）

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

### 4. RAII 原则

```cpp
// 正确：资源由对象管理
class GoodExample {
    std::vector<uint8_t> buffer_;
public:
    GoodExample() : buffer_(1024) {}
    // 无析构函数，自动释放
};
```

### 5. 智能指针选择

| 场景 | 选择 |
| ------ | ------ |
| 独占所有权 | `std::unique_ptr<T>` |
| 共享所有权 | `std::shared_ptr<T>`（尽量少用） |
| 观察（不拥有） | 裸指针 `T*` 或引用 `T&` |

### 6. 线程安全

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

### 7. C++20 核心特性

#### std::span

```cpp
void SendPacket(std::span<const uint8_t> payload) {
    // payload.data(), payload.size()
}
```

#### std::string_view

```cpp
void Log(std::string_view msg) {}  // 替代 const std::string&
```

#### constexpr

```cpp
constexpr size_t kMaxPacketSize = 65535;
constexpr uint32_t Crc32(const char* str) { /* 编译时计算 */ }
```

### 8. AI 与翻译工具使用规范

- **DeepL**：优先用于翻译注释/文档，翻译后人工检查
- **AI 代码生成**：允许，但必须理解每一行、自己测试、在 PR 中声明
- **占位符**：`// * L10N_PENDING [YYYYMMDD:HHMMSS] *`

---

## 第四部分：C/C++ 混合边界（`extern "C"`）

### 1. 头文件写法

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

### 2. C++ 调用 C

```cpp
#include "crypto_c.h"

extern "C" {
    // C 函数已在 crypto_c.h 中声明为 extern "C"
}

auto kp = crypto_keypair_generate();  // 直接调用
```

### 3. 内存管理规则

| 分配 | 释放 | 场景 |
| ------ | ------ | ------ |
| C (`malloc`) | C (`free`) | C API 返回的数据 |
| C++ (`new`) | C++ (`delete`) | C++ 内部对象 |
| C++ (`std::make_unique`) | 自动 | RAII 管理 |

**禁止**：C 分配，C++ 释放（反之亦然）

---

## 第五部分：AI 与翻译工具使用规范

本项目允许使用 AI 和翻译工具，但**必须遵守以下规则**。

### 1. 翻译工具（DeepL）

需要翻译注释或文档时，**优先使用 DeepL**：

1. 先写英文，用 DeepL 翻译成中文
2. 或先写中文，用 DeepL 翻译成英文
3. 翻译后**必须人工检查**，确保术语准确、语气合适

```cpp
// Encrypt message with X25519. 使用 X25519 加密消息。
void Encrypt(const uint8_t* input, size_t len);
```

### 2. AI 代码生成

允许使用 AI（Claude、ChatGPT 等）生成代码，但必须：

| 规则 | 说明 |
| ------ | ------ |
| **理解每一行** | 不懂的代码不要提交 |
| **自己测试** | AI 生成的代码可能有 bug |
| **声明使用** | PR 描述里写「部分代码由 AI 生成」 |
| **遵守本规范** | AI 生成的代码也要通过 `clang-format` 和审查 |

### 3. 禁止行为

- ❌ 直接复制 AI 生成的代码而不理解
- ❌ 用 AI 生成注释但不检查准确性
- ❌ 用 AI 生成测试用例而不验证

### 4. 占位符

如果 DeepL 翻译不出来（或翻译结果明显错误），用占位符：

```cpp
void SomeFunction(); // * L10N_PENDING [DOMIRUS-20260613:235901] *
```

格式：`* L10N_PENDING [作者-YYYYMMDD:HHMMSS] *`

### 5. 为什么要写这些规则

不是因为 AI 不好。
是因为**你必须是代码的主人，AI 只是工具**。

- 你读 ZRTP RFC，AI 帮你写框架 → ✅
- 你不懂 RSA，让 AI 写加密 → ❌
- 你写双语注释，DeepL 辅助翻译 → ✅
- 你让 AI 翻译，但不检查 → ❌

**理解每一行。自己测试。声明使用。**

---

## 附录：快速参考卡

### C 快速参考

| 要做什么 | 写法 |
| ---------- | ------ |
| 分配内存 | `calloc(1, sizeof(T))` |
| 释放内存 | `free(ptr)` |
| 返回错误 | `int` 错误码 + 输出参数 |
| 跨平台 | `#ifdef _WIN32` |
| 线程锁 | `CRITICAL_SECTION` / `pthread_mutex_t` |

### C++ 快速参考

| 要做什么 | 写法 |
| ---------- | ------ |
| 独占指针 | `auto p = std::make_unique<T>()` |
| 只读参数 | `std::span<const T>` 或 `std::string_view` |
| 返回错误 | `Result<T>`（`tl::expected`） |
| 编译时常量 | `constexpr` |
| 数组视图 | `std::span<T>` |
| 线程锁 | `std::lock_guard<std::mutex>` |

---

**文档版本**：2.0
**语言标准**：C11 + C++20
**最后更新**：2026-06-09

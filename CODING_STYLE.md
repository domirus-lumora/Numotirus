# C++20 代码规范指南

感谢你遵守这些规则。不是为了刁难你，是为了让代码能被所有人看懂、改对、不出 bug。

## 1. 命名规则

| 类型 | 风格 | 示例 |
| ------ | ------ | ------ |
| 变量 | `snake_case` | `int peer_id;` |
| 类名 | `PascalCase` | `class KeyExchange {};` |
| 函数名 | `PascalCase` | `void EncryptMessage();` |
| 常量 | `kPascalCase` 或 `CONSTANT_CASE` | `const int kMaxRetry = 3;` |
| 私有成员 | 末尾加 `_` | `int peer_id_;` |
| 命名空间 | `snake_case` | `namespace crypto_utils {}` |
| 枚举类 | `PascalCase` | `enum class ErrorCode {}` |
| 枚举值 | `kPascalCase` | `ErrorCode::kInvalidArgument` |
| 模板参数 | `T` 或 `PascalCase` | `template<typename T>` |

```cpp
int peer_id;           // 正确
int peerID;            // 错误
int m_peerId;          // 错误

class KeyExchange {};  // 正确
class key_exchange {}; // 错误

void EncryptMessage(); // 正确
void encrypt_message();// 错误

const int kMaxRetry = 3;   // 正确
const int MAX_RETRY = 3;   // 正确
```

---

## 2. 禁止项

| 禁止 | 替代 |
| ------ | ------ |
| 裸指针（拥有所有权） | `std::unique_ptr`、`std::shared_ptr` |
| `NULL` | `nullptr` |
| C 风格转换 | `static_cast`、`dynamic_cast`、`const_cast`、`reinterpret_cast` |
| 全局变量 | `constexpr` 常量、单例模式（谨慎使用） |
| 手动 `new`/`delete` | RAII 容器 |
| C 风格数组 | `std::array`、`std::vector`、`std::span` |
| 宏（常量/函数） | `constexpr`、`consteval`、模板 |
| 裸 `enum` | `enum class` |

```cpp
// 错误
int* p = new int;
if (ptr == NULL) {}
int x = (int)y;
int arr[100];

// 正确
auto p = std::make_unique<int>();
if (ptr == nullptr) {}
int x = static_cast<int>(y);
std::array<int, 100> arr;
```

---

## 3. 头文件规范

```cpp
// 使用 #pragma once（简洁，所有编译器都支持）
#pragma once

// 或使用 include guard（传统写法）
#ifndef MODULE_NAME_H_
#define MODULE_NAME_H_
// ...
#endif  // MODULE_NAME_H_
```

**选择**：新项目用 `#pragma once`，需要最高可移植性时用 include guard。

---

## 4. #include 顺序

```cpp
// 1. 当前文件对应的头文件（.cpp 中）
#include "my_class.h"

// 2. C 标准库
#include <cstddef>
#include <cstring>

// 3. C++ 标准库
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 4. 第三方库（按字母序）
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <tl/expected.hpp>

// 5. 项目内部头文件（按字母序）
#include "base/utils.h"
#include "network/peer.h"
```

每组之间空一行。

---

## 5. 注释规范

中英双语，英文在前：

```cpp
// Encrypt message with X25519. 使用 X25519 加密消息。
void Encrypt(const std::string& input);

// Returns decrypted data on success, error code on failure.
// 成功时返回解密数据，失败时返回错误码。
Result<std::vector<uint8_t>> Decrypt(std::span<const uint8_t> ciphertext);
```

**公共接口必须注释**，说明：

- 函数作用
- 参数含义
- 返回值含义
- 可能抛出的异常或错误码

**占位符**（如果不会写某段注释的英文或中文）：

```cpp
void SomeFunction(); // * L10N_PENDING [YYYYMMDD:HHMMSS] *
```

---

## 6. 格式化

使用 `clang-format`，配置文件 `.clang-format`：

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
Language: Cpp
Standard: c++20
```

提交前运行：

```bash
clang-format -i src/*.cpp src/*.h
```

---

## 7. 错误处理

**C++20 没有 `std::expected`（C++23 才有），项目统一使用 `tl::expected`。**

### 安装 tl::expected

```bash
# vcpkg
vcpkg install tl-expected

# Conan
conan install tl-expected/1.1.0

# 或直接下载头文件
# https://github.com/TartanLlama/expected
```

### 使用示例

```cpp
#include <tl/expected.hpp>

using tl::expected;

// 错误码定义
enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument,
    kNetworkError,
    kDecryptionFailed,
};

// 返回结果类型
template<typename T>
using Result = expected<T, ErrorCode>;

// 使用示例
Result<std::vector<uint8_t>> DecryptMessage(
    std::span<const uint8_t> ciphertext,
    const std::array<uint8_t, 32>& private_key) {
    
    if (ciphertext.empty()) {
        return std::unexpected(ErrorCode::kInvalidArgument);
    }
    
    // 解密逻辑...
    return decrypted_data;
}

// 调用方处理
auto result = DecryptMessage(data, key);
if (result.has_value()) {
    Process(result.value());
} else {
    LogError("Decrypt failed: ", static_cast<int>(result.error()));
}
```

### 备选方案

仅在 `tl::expected` 不可用时使用：

```cpp
// 方案二：optional + 错误码分离
struct DecryptResult {
    std::optional<std::vector<uint8_t>> data;
    ErrorCode error = ErrorCode::kSuccess;
};

// 方案三：异常（仅在构造函数、运算符重载中使用）
std::vector<uint8_t> DecryptOrThrow(std::span<const uint8_t> data) {
    if (data.empty()) throw std::invalid_argument("empty data");
    // ...
}
```

---

## 8. RAII 原则

**所有资源必须由对象管理，禁止手动 `new`/`delete`**

```cpp
// 错误：手动管理
class BadExample {
    uint8_t* buffer_;
public:
    BadExample() : buffer_(new uint8_t[1024]) {}
    ~BadExample() { delete[] buffer_; }  // 容易忘记，且不异常安全
};

// 正确：RAII
class GoodExample {
    std::vector<uint8_t> buffer_;
public:
    GoodExample() : buffer_(1024) {}  // 自动管理
    // 无析构函数，自动释放
};
```

---

## 9. 智能指针选择

| 场景 | 选择 |
| ------ | ------ |
| 独占所有权 | `std::unique_ptr<T>` |
| 共享所有权 | `std::shared_ptr<T>`（尽量少用） |
| 弱引用 | `std::weak_ptr<T>` |
| 工厂函数返回 | `std::unique_ptr<T>` |
| 观察（不拥有） | 裸指针 `T*` 或引用 `T&` |

```cpp
// 独占所有权示例
class Connection {
    std::unique_ptr<Socket> socket_;
public:
    Connection() : socket_(std::make_unique<Socket>()) {}
    // 移动可行，复制禁止
    Connection(Connection&&) = default;
};

// 工厂函数
std::unique_ptr<KeyPair> CreateKeyPair() {
    return std::make_unique<KeyPair>();
}

// 观察者模式（不拥有）
void ProcessData(const Data& data, Logger* logger) {
    if (logger) {
        logger->Log("Processing");
    }
}
```

---

## 10. C++20 核心特性

### std::span（替代数组指针）

```cpp
// 参数传递（首选）
void SendPacket(std::span<const uint8_t> payload) {
    // payload.data(), payload.size()
}

// 从不同容器构造
std::vector<uint8_t> vec = {1,2,3};
std::array<uint8_t, 3> arr = {1,2,3};
uint8_t c_arr[] = {1,2,3};

SendPacket(vec);      // OK
SendPacket(arr);      // OK
SendPacket(c_arr);    // OK

// 动态截取
void ProcessHeader(std::span<uint8_t> buffer, size_t offset) {
    auto header = buffer.subspan(offset, 64);
    // 调用者保证 offset+64 <= buffer.size()
}
```

### std::string_view（替代 const std::string&）

```cpp
// 旧方式
void Log(const std::string& msg) {}

// 新方式（更高效）
void Log(std::string_view msg) {}

// 使用
Log("hello");           // OK，无临时 string 对象
Log(std::string("hi")); // OK
```

### 概念 (concepts)

```cpp
// 定义概念
template<typename T>
concept ByteContainer = requires(T t) {
    { t.data() } -> std::convertible_to<const uint8_t*>;
    { t.size() } -> std::convertible_to<size_t>;
};

// 使用概念（简洁版）
void ProcessBytes(ByteContainer auto&& container) {
    auto span = std::span(container.data(), container.size());
    // 处理...
}

// 使用概念（完整版）
template<ByteContainer T>
void ProcessBytes(T&& container);
```

### consteval（强制编译时执行）

```cpp
// 必须编译时执行
consteval uint32_t CompileTimeHash(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash = (hash ^ *str++) * 16777619u;
    }
    return hash;
}

// 编译时验证
static_assert(CompileTimeHash("test") == 0x3c4e2a6b);

// 错误：不能在运行时调用
// uint32_t runtime = CompileTimeHash(getenv("str"));  // 编译错误
```

### 协程（按需使用）

```cpp
#include <coroutine>

// 简单协程任务
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

// 异步操作示例
Task AsyncRead(Socket& sock, std::span<uint8_t> buffer) {
    co_await sock.async_read_some(buffer);
    // co_await 后继续执行
}
```

**协程使用原则**：

- 仅在需要异步 I/O 时使用
- 必须有良好的 RAII 管理
- 注意协程栈变量的生命周期

---

## 11. 线程安全

```cpp
#include <mutex>
#include <shared_mutex>

class ThreadSafeCounter {
private:
    mutable std::mutex mutex_;  // mutable 允许 const 方法加锁
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

// 读多写少场景使用 shared_mutex
class ThreadSafeCache {
private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::vector<uint8_t>> cache_;
    
public:
    std::optional<std::vector<uint8_t>> Get(std::string_view key) const {
        std::shared_lock lock(mutex_);  // 共享锁
        auto it = cache_.find(std::string(key));
        if (it != cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    void Set(std::string key, std::vector<uint8_t> value) {
        std::unique_lock lock(mutex_);  // 独占锁
        cache_[std::move(key)] = std::move(value);
    }
};
```

**锁选择指南**：

| 场景 | 推荐 |
| ------ | ------ |
| 普通场景 | `std::mutex` + `std::lock_guard` |
| 读多写少 | `std::shared_mutex` + `std::shared_lock` |
| 需要递归锁 | `std::recursive_mutex`（尽量避免） |
| 定时尝试 | `std::timed_mutex` |

---

## 12. constexpr 和 consteval

```cpp
// 编译时常量
constexpr size_t kMaxPacketSize = 65535;
constexpr double kPi = 3.141592653589793;

// 编译时函数（C++20 支持更多特性）
constexpr uint32_t Crc32(const char* str) {
    uint32_t crc = 0xFFFFFFFF;
    for (; *str; ++str) {
        crc = (crc >> 8) ^ kCrcTable[(crc ^ *str) & 0xFF];
    }
    return ~crc;
}

static_assert(Crc32("test") == 0xD87F7E0C);

// C++20: constexpr 支持动态分配
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

// constinit: 保证编译时初始化（避免静态初始化顺序问题）
constinit std::array<int, 3> kValues = {1, 2, 3};
```

## 13. AI 与翻译工具使用规范

### DeepL 使用规则

需要翻译注释或文档时，**优先使用 DeepL**：

1. 先写英文，用 DeepL 翻译成中文
2. 或先写中文，用 DeepL 翻译成英文
3. 翻译后**必须人工检查**，确保术语准确、语气合适

### AI 代码生成规则

允许使用 AI（Copilot、ChatGPT 等）生成代码，但必须：

1. **理解每一行**：不懂的代码不要提交
2. **自己测试**：AI 生成的代码可能有 bug
3. **声明使用**：PR 描述里写「部分代码由 AI 生成」
4. **遵守本规范**：AI 生成的代码也要通过 clang-format 和审查

### 占位符

如果 DeepL 翻译不出来（或翻译结果明显错误），用占位符：

```cpp
void SomeFunction(); // * L10N_PENDING [DOMIRUS-20260613:235901] *
```

## 14. 测试规范

### 文件结构

```text
test/
├── encryption_test.cpp
├── network_test.cpp
└── utils_test.cpp
```

### 测试代码示例

```cpp
// test/encryption_test.cpp
#include <gtest/gtest.h>
#include "encryption.h"

class EncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试前执行
        key_ = GenerateTestKey();
    }
    
    void TearDown() override {
        // 每个测试后执行（可选）
    }
    
    std::array<uint8_t, 32> key_;
};

// 测试命名：FunctionName_Scenario_ExpectedBehavior
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

### 测试覆盖率要求

| 模块类型 | 行覆盖率 | 分支覆盖率 |
| ---------- | ---------- | ------------ |
| 核心加密/解密 | ≥ 90% | ≥ 80% |
| 网络 I/O | ≥ 80% | ≥ 70% |
| 工具类 | ≥ 85% | ≥ 75% |

---

## 15. 审查清单

代码提交前逐项检查：

### 风格规范

- [ ] 变量名 `snake_case`
- [ ] 类名/函数名 `PascalCase`
- [ ] 常量 `kPascalCase`
- [ ] 私有成员末尾 `_`
- [ ] 通过 `clang-format`

### 禁止项

- [ ] 无裸指针（拥有所有权）
- [ ] 无 `NULL`（用 `nullptr`）
- [ ] 无 C 风格转换
- [ ] 无全局变量（`constexpr` 除外）
- [ ] 无手动 `new`/`delete`

### 内存与资源

- [ ] 所有资源使用 RAII 管理
- [ ] 智能指针选择正确
- [ ] 无内存泄漏
- [ ] 无悬空引用/指针

### 并发

- [ ] 共享数据有锁保护
- [ ] 无数据竞争
- [ ] 锁粒度合理

### 错误处理

- [ ] 使用 `tl::expected` 返回错误
- [ ] 所有 `Result` 都被检查
- [ ] 错误码有意义

### 注释与文档

- [ ] 公共接口有双语注释
- [ ] 注释说明了功能、参数、返回值
- [ ] 复杂逻辑有解释注释

### 测试

- [ ] 新功能有单元测试
- [ ] 正常路径测试通过
- [ ] 错误路径测试通过
- [ ] 边界条件测试通过

### 其他

- [ ] `#include` 顺序正确
- [ ] 使用 C++20 特性（不用旧式写法）

---

## 16. Markdown 规范

本项目所有 `.md` 文件必须通过 `markdownlint` 检查。

### 必须规则

| 规则 | 说明 |
| ------ | ------ |
| MD001 | 标题级别只能一次递增一级 |
| MD003 | 标题风格统一，用 `#` 开头 |
| MD004 | 无序列表用 `-` |
| MD009 | 行尾不能有空格 |
| MD010 | 禁用 Tab，用空格缩进（4 空格） |
| MD012 | 不能有连续多个空行（最多 1 个） |
| MD022 | 标题前后必须有空行 |
| MD031 | 代码块前后必须有空行 |
| MD032 | 列表前后必须有空行 |
| MD035 | 水平线用 `---` |
| MD040 | 代码块必须指定语言 |
| MD041 | 文件第一行必须是顶级标题 |
| MD047 | 文件末尾必须有且仅有一个换行 |

### 建议规则

| 规则 | 说明 |
| ------ | ------ |
| MD013 | 行长度不超过 120 字符 |
| MD024 | 避免重复的标题内容 |
| MD033 | 避免内联 HTML |
| MD034 | 用 `[文字](URL)` 代替裸 URL |
| MD036 | 禁止用强调代替标题 |

### 检查命令

```bash
# 检查所有 md 文件
markdownlint *.md docs/*.md

# 自动修复部分问题
markdownlint --fix *.md
```

**VS Code 用户**：安装 `markdownlint` 插件，会自动检查。

---

## 附录 A：常用代码模板

### 类模板

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
    std::unique_ptr<Impl> impl_;  // Pimpl 惯用法
};

}  // namespace my_namespace
```

### 函数模板

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

## 附录 B：快速参考卡

| 要做什么 | 写法 |
| ---------- | ------ |
| 独占指针 | `auto p = std::make_unique<T>(args...)` |
| 只读参数 | `std::span<const T>` 或 `std::string_view` |
| 返回错误 | `Result<T>`（`tl::expected`） |
| 编译时常量 | `constexpr` |
| 强制编译时 | `consteval` |
| 数组视图 | `std::span<T>` |
| 只读字符串 | `std::string_view` |
| 线程安全锁 | `std::lock_guard<std::mutex>` |
| 读多写少锁 | `std::shared_lock` / `std::unique_lock` |
| 禁用拷贝 | `= delete` |
| 移动语义 | `= default` |

---

**文档版本**：1.0  
**C++ 标准**：C++20  
**最后更新**：2026-06-02

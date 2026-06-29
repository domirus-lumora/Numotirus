# Tesla Coil Transform (TCT)

## 功能

基于 EML 算子（e^x - ln(w)）的三层可逆变换，用于在标准加密前对数据进行混淆。TCT 本身不提供安全性，安全性由外层的 XChaCha20-Poly1305（AEAD）保证。适用于需要额外私有混淆层的实验场景。

## 语言

C++20

## 依赖

- libsodium（提供 XChaCha20-Poly1305 加密）
- C++17 或更高版本编译器
- 支持 `std::array`、`std::vector`、`std::optional`

## 运行

### 安装 libsodium

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

### 编译

```bash
g++ -std=c++17 tct.cpp tct_demo.cpp -lsodium -o tct_demo.exe
```

### 运行测试

```bash
./tct_demo.exe
```

## 测试

测试程序 `tct_demo.cpp` 执行 100,000 次随机往返测试：

- 随机生成明文（范围 [-1, 1]）
- 随机生成 nonce（24 字节）
- 加密 → 解密
- 统计成功率、最大误差、平均误差

## 预编译版本（仅供测试）

仓库中包含 `tct_demo.exe`（Windows 版本），由模块作者编译提供。

**注意**：本 exe 仅供快速测试 TCT 功能使用。正式使用请自行从源码编译。

## 如何验证它是对的

1. 编译并运行 `tct_demo.exe`
2. 观察输出：
   - `Valid roundtrips: 100000 / 100000`（全部成功）
   - `Failures (NaN/overflow): 0`（无数值错误）
   - `Max error: < 1e-5`（误差在百万分之一量级）

## 示例

```cpp
#include "tct.hpp"

int main() {
    // 密钥（双方共享）
    uint64_t secret = 0x123456789ABCDEF0ULL;
    std::array<uint8_t, 32> key{};
    randombytes_buf(key.data(), key.size());

    tct::TeslaCoilTransform tct(secret, key);

    // 原始数据（0-255 的字节）
    uint8_t original = 42;
    int64_t max_val = 255;

    // 缩放到 [-1, 1]
    double plain = tct.scale_to_plaintext(original, max_val);

    // 随机 nonce
    tct::Nonce nonce{};
    randombytes_buf(nonce.data(), nonce.size());

    // 加密（TCT 变换 + XChaCha20-Poly1305）
    auto ciphertext = tct.encrypt(plain, nonce);

    // 解密
    auto decrypted = tct.decrypt(ciphertext);

    if (decrypted) {
        int64_t recovered = tct.scale_from_plaintext(*decrypted, max_val);
        // recovered == original
    }

    return 0;
}
```

## 输入 / 输出

- **输入（明文）**：`double`，范围 [-1, 1]（使用 `scale_to_plaintext` 将原始整数归一化）
- **输出（密文）**：`std::vector<uint8_t>`，格式为 `[nonce(24字节)] + [XChaCha20-Poly1305密文]`
- **密钥**：`uint64_t secret`（混淆层密钥）+ `std::array<uint8_t, 32> key`（加密密钥）

## 引用

特别感谢[EML论文](https://arxiv.org/html/2603.21852v2)作者的发现。

## 作者

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

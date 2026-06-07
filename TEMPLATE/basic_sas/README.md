# ZRTP Basic SAS

## 功能

从共享秘密派生 4 组 4 位数字，用于 P2P 通信中双方口头比对身份，防止中间人攻击。

## 语言

C99

## 依赖

- libsodium 1.0.18+

## 运行

```bash
gcc -c sas.c -o sas.o -lsodium
```

## 测试

```bash
gcc -o test_sas test_sas.c sas.c -lsodium
./test_sas
```

## 如何验证它是对的

1. 运行测试程序，输入相同的共享秘密（如 32 字节随机数）
2. 两次生成的 SAS 码应完全相同
3. 改变共享秘密的任何一位，SAS 码应完全不同

## 示例

```c
#include "sas.h"
#include <sodium.h>
#include <stdio.h>

int main() {
    if (sodium_init() < 0) return 1;
    
    // 模拟共享秘密（实际从 X25519 密钥交换获得）
    uint8_t shared_secret[32];
    randombytes_buf(shared_secret, 32);
    
    char sas[20];
    if (sas_generate(shared_secret, 32, sas, sizeof(sas)) == 0) {
        printf("ZRTP SAS: %s\n", sas);
    }
    return 0;
}
```

## 输入 / 输出

- 输入：32 字节共享秘密（`const uint8_t*`）
- 输出：20 字节字符串，格式 `"1234 5678 9012 3456"`（含结尾 `\0`）

## 作者

Domirus / [domirus-lumora](https://github.com/domirus-lumora)

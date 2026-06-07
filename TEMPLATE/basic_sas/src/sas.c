// core/protocol/sas.c
// SAS (Short Authentication String) generation implementation.
// SAS（短认证字符串）生成实现。

#include "sas.h"
#include <sodium.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Generate 4 groups of 4-digit numbers from shared secret.
// 从共享秘密派生 4 组 4 位数字。
// Format: "1234 5678 9012 3456". 格式："1234 5678 9012 3456"。
int sas_generate(const uint8_t* shared_secret, size_t secret_len, char* out, size_t out_size) {
    // Validate parameters. 检查参数有效性。
    if (!shared_secret || secret_len != 32 || !out || out_size < 20) {
        return -1;
    }

    // Derive 16 bytes from shared secret using BLAKE2b.
    // 使用 BLAKE2b 从共享秘密派生 16 字节数据。
    uint8_t sas_data[16];
    crypto_generichash(sas_data, sizeof(sas_data),
                       shared_secret, secret_len,
                       NULL, 0);

    // Split into 4 groups of 4-digit numbers (0-9999).
    // 拆分成 4 组 4 位数字（0-9999）。
    // Each group uses 14 bits (2^14 = 16384 > 10000).
    // 每组使用 14 位信息（2^14 = 16384 > 10000）。
    uint32_t codes[4] = {0};

    for (int i = 0; i < 4; i++) {
        uint16_t val = (sas_data[i * 2] << 8) | sas_data[i * 2 + 1];
        codes[i] = val % 10000;
    }

    // Format as "1234 5678 9012 3456". 格式化为 "1234 5678 9012 3456"。
    snprintf(out, out_size, "%04u %04u %04u %04u",
             codes[0], codes[1], codes[2], codes[3]);

    return 0;
}
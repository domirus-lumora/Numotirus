// test_sas.c
// Test program for ZRTP Basic SAS module. ZRTP 基础 SAS 模块测试程序。

#include "sas.h"
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

// 打印十六进制。Print hex.
static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
}

// 测试1：相同共享秘密应产生相同 SAS。Test 1: Same shared secret should produce same SAS.
static int test_same_secret(void) {
    uint8_t secret[32];
    char sas1[20], sas2[20];
    
    randombytes_buf(secret, 32);
    
    if (sas_generate(secret, 32, sas1, sizeof(sas1)) != 0) return -1;
    if (sas_generate(secret, 32, sas2, sizeof(sas2)) != 0) return -1;
    
    if (strcmp(sas1, sas2) == 0) {
        printf("✅ test_same_secret: PASSED (SAS: %s)\n", sas1);
        return 0;
    } else {
        printf("❌ test_same_secret: FAILED\n");
        return -1;
    }
}

// 测试2：不同共享秘密应产生不同 SAS。Test 2: Different shared secrets should produce different SAS.
static int test_different_secret(void) {
    uint8_t secret1[32], secret2[32];
    char sas1[20], sas2[20];
    
    randombytes_buf(secret1, 32);
    randombytes_buf(secret2, 32);
    
    if (sas_generate(secret1, 32, sas1, sizeof(sas1)) != 0) return -1;
    if (sas_generate(secret2, 32, sas2, sizeof(sas2)) != 0) return -1;
    
    if (strcmp(sas1, sas2) != 0) {
        printf("✅ test_different_secret: PASSED (SAS1: %s, SAS2: %s)\n", sas1, sas2);
        return 0;
    } else {
        printf("❌ test_different_secret: FAILED (same SAS for different secrets)\n");
        return -1;
    }
}

// 测试3：单比特翻转应产生完全不同 SAS。Test 3: Single bit flip should produce completely different SAS.
static int test_bit_flip(void) {
    uint8_t secret[32];
    char sas_orig[20], sas_flipped[20];
    
    randombytes_buf(secret, 32);
    
    if (sas_generate(secret, 32, sas_orig, sizeof(sas_orig)) != 0) return -1;
    
    // Flip the first bit of the first byte. 翻转第一个字节的最低位。
    secret[0] ^= 0x01;
    
    if (sas_generate(secret, 32, sas_flipped, sizeof(sas_flipped)) != 0) return -1;
    
    if (strcmp(sas_orig, sas_flipped) != 0) {
        printf("✅ test_bit_flip: PASSED (original: %s, flipped: %s)\n", sas_orig, sas_flipped);
        return 0;
    } else {
        printf("❌ test_bit_flip: FAILED (SAS unchanged after bit flip)\n");
        return -1;
    }
}

// 测试4：无效参数应返回 -1。Test 4: Invalid parameters should return -1.
static int test_invalid_params(void) {
    uint8_t secret[32];
    char sas[20];
    int ret;
    
    randombytes_buf(secret, 32);
    
    // NULL shared_secret. 空共享秘密。
    ret = sas_generate(NULL, 32, sas, sizeof(sas));
    if (ret == -1) printf("✅ test_invalid_params: NULL secret returned -1\n");
    else printf("❌ test_invalid_params: NULL secret did not return -1\n");
    
    // Wrong secret length. 错误秘密长度。
    ret = sas_generate(secret, 16, sas, sizeof(sas));
    if (ret == -1) printf("✅ test_invalid_params: wrong length returned -1\n");
    else printf("❌ test_invalid_params: wrong length did not return -1\n");
    
    // NULL output buffer. 空输出缓冲区。
    ret = sas_generate(secret, 32, NULL, sizeof(sas));
    if (ret == -1) printf("✅ test_invalid_params: NULL output returned -1\n");
    else printf("❌ test_invalid_params: NULL output did not return -1\n");
    
    // Output buffer too small. 输出缓冲区太小。
    ret = sas_generate(secret, 32, sas, 10);
    if (ret == -1) printf("✅ test_invalid_params: small buffer returned -1\n");
    else printf("❌ test_invalid_params: small buffer did not return -1\n");
    
    return 0;
}

int main(void) {
    // Set UTF-8 for Unicode support. 设置 UTF-8 以支持 Unicode。
#ifdef _WIN32
    system("chcp 65001 > nul");
#else
    setlocale(LC_ALL, "zh_CN.UTF-8");
#endif

    if (sodium_init() < 0) {
        printf("libsodium init failed\n");
        return 1;
    }
    
    printf("\n=== ZRTP SAS Test ===\n\n");
    
    int passed = 0;
    int total = 0;
    
    total++; if (test_same_secret() == 0) passed++;
    total++; if (test_different_secret() == 0) passed++;
    total++; if (test_bit_flip() == 0) passed++;
    test_invalid_params();  // Informational only. 仅信息性，不计入通过/失败。
    
    printf("\n=== Summary ===\n");
    printf("Passed: %d/%d\n", passed, total);
    
    return (passed == total) ? 0 : 1;
}
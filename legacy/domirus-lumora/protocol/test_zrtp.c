// test_zrtp.c
// ZRTP module test program. ZRTP 模块测试程序。

#include "zrtp.h"
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

// Test 1: Same keypair should generate same SAS.
// 测试1：相同密钥对应生成相同 SAS。
static int test_same_sas(void) {
  uint8_t pk1[32], sk1[32];
  uint8_t pk2[32], sk2[32];
  crypto_box_keypair(pk1, sk1);
  crypto_box_keypair(pk2, sk2);

  zrtp_session_t* alice = zrtp_session_new();
  zrtp_session_t* bob = zrtp_session_new();

  zrtp_session_set_keypair(alice, pk1, sk1);
  zrtp_session_set_peer_public(alice, pk2);

  zrtp_session_set_keypair(bob, pk2, sk2);
  zrtp_session_set_peer_public(bob, pk1);

  if (zrtp_session_key_exchange(alice) != ZRTP_SUCCESS) {
    zrtp_session_free(alice);
    zrtp_session_free(bob);
    return -1;
  }
  if (zrtp_session_key_exchange(bob) != ZRTP_SUCCESS) {
    zrtp_session_free(alice);
    zrtp_session_free(bob);
    return -1;
  }

  const char* sas_alice = zrtp_session_get_sas(alice);
  const char* sas_bob = zrtp_session_get_sas(bob);

  int ret = (strcmp(sas_alice, sas_bob) == 0);
  printf("%s test_same_sas: %s\n", ret ? "✅" : "❌", ret ? "PASSED" : "FAILED");
  if (ret) printf("   SAS: %s\n", sas_alice);

  zrtp_session_free(alice);
  zrtp_session_free(bob);
  return ret ? 0 : -1;
}

// Test 2: Different keypairs should generate different SAS.
// 测试2：不同密钥对应生成不同 SAS。
static int test_different_sas(void) {
  uint8_t pk1[32], sk1[32];
  uint8_t pk2[32], sk2[32];
  uint8_t pk3[32], sk3[32];
  crypto_box_keypair(pk1, sk1);
  crypto_box_keypair(pk2, sk2);
  crypto_box_keypair(pk3, sk3);

  zrtp_session_t* alice = zrtp_session_new();
  zrtp_session_t* bob = zrtp_session_new();

  // First exchange with peer2. 第一次与 peer2 交换。
  zrtp_session_set_keypair(alice, pk1, sk1);
  zrtp_session_set_peer_public(alice, pk2);
  zrtp_session_set_keypair(bob, pk2, sk2);
  zrtp_session_set_peer_public(bob, pk1);
  zrtp_session_key_exchange(alice);
  zrtp_session_key_exchange(bob);
  const char* sas1 = zrtp_session_get_sas(alice);

  // Second exchange with peer3 using a new session for alice.
  // 使用新的 alice 会话与 peer3 交换。
  zrtp_session_t* alice2 = zrtp_session_new();
  zrtp_session_set_keypair(alice2, pk1, sk1);
  zrtp_session_set_peer_public(alice2, pk3);
  zrtp_session_key_exchange(alice2);
  const char* sas2 = zrtp_session_get_sas(alice2);

  int ret = (strcmp(sas1, sas2) != 0);
  printf("%s test_different_sas: %s\n", ret ? "✅" : "❌", ret ? "PASSED" : "FAILED");
  if (ret) printf("   SAS1: %s\n   SAS2: %s\n", sas1, sas2);

  zrtp_session_free(alice);
  zrtp_session_free(bob);
  zrtp_session_free(alice2);
  return ret ? 0 : -1;
}

// Test 3: Verified flag. 测试3：验证标志。
static int test_verified_flag(void) {
  uint8_t pk1[32], sk1[32];
  uint8_t pk2[32], sk2[32];
  crypto_box_keypair(pk1, sk1);
  crypto_box_keypair(pk2, sk2);

  zrtp_session_t* sess = zrtp_session_new();
  zrtp_session_set_keypair(sess, pk1, sk1);
  zrtp_session_set_peer_public(sess, pk2);
  zrtp_session_key_exchange(sess);

  int before = zrtp_session_is_verified(sess);
  zrtp_session_mark_verified(sess);
  int after = zrtp_session_is_verified(sess);

  int ret = (before == 0 && after == 1);
  printf("%s test_verified_flag: %s\n", ret ? "✅" : "❌", ret ? "PASSED" : "FAILED");
  if (ret) printf("   Verified: %d -> %d\n", before, after);

  zrtp_session_free(sess);
  return ret ? 0 : -1;
}

// Test 4: Trust store. 测试4：信任存储。
static int test_trust_store(void) {
  // Remove old file to avoid interference. 删除旧文件避免干扰。
  remove("trusted_peers.bin");

  const char* peer_id = "alice@example.com";
  uint8_t pubkey[32], secret[32];
  randombytes_buf(pubkey, 32);
  randombytes_buf(secret, 32);

  if (zrtp_trust_store_save(peer_id, pubkey, secret) != ZRTP_SUCCESS) {
    printf("❌ test_trust_store: SAVE FAILED\n");
    return -1;
  }

  uint8_t loaded_pub[32], loaded_sec[32];
  if (zrtp_trust_store_load(peer_id, loaded_pub, loaded_sec) != ZRTP_SUCCESS) {
    printf("❌ test_trust_store: LOAD FAILED\n");
    return -1;
  }

  int ret = (memcmp(pubkey, loaded_pub, 32) == 0 &&
             memcmp(secret, loaded_sec, 32) == 0);
  printf("%s test_trust_store: %s\n", ret ? "✅" : "❌", ret ? "PASSED" : "FAILED");
  return ret ? 0 : -1;
}

int main(void) {
  // Set UTF-8 for Unicode support (Windows). 设置 UTF-8 以支持 Unicode（Windows）。
#ifdef _WIN32
  system("chcp 65001 > nul");
#endif
  setlocale(LC_ALL, "zh_CN.UTF-8");

  if (sodium_init() < 0) {
    printf("libsodium init failed\n");
    return 1;
  }

  printf("\n=== ZRTP Module Test ===\n\n");

  int passed = 0;
  int total = 0;

  ++total; if (test_same_sas() == 0) ++passed;
  ++total; if (test_different_sas() == 0) ++passed;
  ++total; if (test_verified_flag() == 0) ++passed;
  ++total; if (test_trust_store() == 0) ++passed;

  printf("\n=== Summary ===\n");
  printf("Passed: %d/%d\n", passed, total);

  return (passed == total) ? 0 : 1;
}
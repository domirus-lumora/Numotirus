// core/protocol/zrtp.c
// ZRTP session implementation with multi-entry trust store and memory zeroization.
// ZRTP 会话实现，支持多条目信任存储和内存零化。
// SPDX-License-Identifier: Apache-2.0

#include "zrtp.h"
#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ZRTP session structure. ZRTP 会话结构。
struct zrtp_session_t {
    uint8_t my_pk[32];      // Own public key. 自己的公钥。
    uint8_t my_sk[32];      // Own secret key. 自己的私钥。
    uint8_t peer_pk[32];    // Peer's public key. 对方的公钥。
    uint8_t rx_key[32];     // Receive key (decrypt). 接收密钥（解密）。
    uint8_t tx_key[32];     // Transmit key (encrypt). 发送密钥（加密）。
    char sas[20];           // SAS string. SAS 字符串。
    int verified;           // Verified flag (0 = not verified, 1 = verified).
                            // 已验证标志（0=未验证，1=已验证）。
};

// Compare two buffers of length 32. 比较两个长度为 32 的缓冲区。
static int compare_32(const uint8_t a[32], const uint8_t b[32]) {
    return memcmp(a, b, 32);
}

// Create a new ZRTP session. 创建新的 ZRTP 会话。
zrtp_session_t* zrtp_session_new(void) {
    return (zrtp_session_t*)calloc(1, sizeof(zrtp_session_t));
}

// Destroy a ZRTP session and free resources. 销毁 ZRTP 会话并释放资源。
void zrtp_session_free(zrtp_session_t* sess) {
    if (sess) {
        // Wipe sensitive data before freeing. 释放前清除敏感数据。
        sodium_memzero(sess, sizeof(zrtp_session_t));
        free(sess);
    }
}

// Set own keypair. 设置自己的密钥对。
void zrtp_session_set_keypair(zrtp_session_t* sess,
                              const uint8_t pubkey[32],
                              const uint8_t seckey[32]) {
    if (!sess) return;
    memcpy(sess->my_pk, pubkey, 32);
    memcpy(sess->my_sk, seckey, 32);
}

// Set peer's public key. 设置对方的公钥。
void zrtp_session_set_peer_public(zrtp_session_t* sess,
                                  const uint8_t peer_pubkey[32]) {
    if (!sess) return;
    memcpy(sess->peer_pk, peer_pubkey, 32);
}

// Perform key exchange using crypto_kx. 使用 crypto_kx 执行密钥交换。
zrtp_error_t zrtp_session_key_exchange(zrtp_session_t* sess) {
    if (!sess) return ZRTP_ERROR_INVALID_ARGUMENT;

    uint8_t rx[32], tx[32];
    int ret;

    // Role negotiation based on lexicographic order of public keys.
    // 基于公钥字典序的角色协商。
    if (compare_32(sess->my_pk, sess->peer_pk) < 0) {
        ret = crypto_kx_client_session_keys(rx, tx,
                                            sess->my_pk, sess->my_sk,
                                            sess->peer_pk);
    } else {
        ret = crypto_kx_server_session_keys(rx, tx,
                                            sess->my_pk, sess->my_sk,
                                            sess->peer_pk);
    }
    if (ret != 0) return ZRTP_ERROR_KEY_EXCHANGE_FAILED;

    // Combine and sort to get identical SAS on both sides.
    // 组合并排序，使双方得到相同的 SAS。
    uint8_t combined[64];
    if (compare_32(rx, tx) < 0) {
        memcpy(combined, rx, 32);
        memcpy(combined + 32, tx, 32);
    } else {
        memcpy(combined, tx, 32);
        memcpy(combined + 32, rx, 32);
    }

    // Generate SAS using BLAKE2b. 使用 BLAKE2b 生成 SAS。
    uint8_t hash[32];
    crypto_generichash(hash, 32, combined, 64, NULL, 0);

    // Convert to 4 groups of 4-digit numbers. 转换为 4 组 4 位数字。
    uint16_t parts[4];
    for (int i = 0; i < 4; ++i) {
        parts[i] = (hash[i * 2] << 8) | hash[i * 2 + 1];
    }
    snprintf(sess->sas, sizeof(sess->sas), "%04u %04u %04u %04u",
             parts[0] % 10000, parts[1] % 10000,
             parts[2] % 10000, parts[3] % 10000);

    // Store session keys. 存储会话密钥。
    memcpy(sess->rx_key, rx, 32);
    memcpy(sess->tx_key, tx, 32);

    // Wipe stack secrets. 清除栈上秘密。
    sodium_memzero(rx, 32);
    sodium_memzero(tx, 32);
    sodium_memzero(combined, 64);
    sodium_memzero(hash, 32);

    return ZRTP_SUCCESS;
}

// Get SAS string. 获取 SAS 字符串。
const char* zrtp_session_get_sas(const zrtp_session_t* sess) {
    return sess ? sess->sas : NULL;
}

// Mark session as verified. 标记会话已验证。
void zrtp_session_mark_verified(zrtp_session_t* sess) {
    if (sess) sess->verified = 1;
}

// Check if session is verified. 检查会话是否已验证。
int zrtp_session_is_verified(const zrtp_session_t* sess) {
    return sess ? sess->verified : 0;
}

// Get receive key. 获取接收密钥。
const uint8_t* zrtp_session_get_rx_key(const zrtp_session_t* sess) {
    return sess ? sess->rx_key : NULL;
}

// Get transmit key. 获取发送密钥。
const uint8_t* zrtp_session_get_tx_key(const zrtp_session_t* sess) {
    return sess ? sess->tx_key : NULL;
}

// Trust store file name. 信任存储文件名。
static const char* kTrustStoreFile = "trusted_peers.bin";

// Save trust entry (append mode). 保存信任条目（追加模式）。
zrtp_error_t zrtp_trust_store_save(const char* peer_id,
                                   const uint8_t peer_pubkey[32],
                                   const uint8_t shared_secret[32]) {
    if (!peer_id || !peer_pubkey || !shared_secret) {
        return ZRTP_ERROR_INVALID_ARGUMENT;
    }

    // Open in append-binary mode to support multiple entries.
    // 以追加二进制模式打开，支持多条目。
    FILE* f = fopen(kTrustStoreFile, "ab");
    if (!f) return ZRTP_ERROR_TRUST_STORE_IO;

    char id[32] = {0};
    strncpy(id, peer_id, 31);
    fwrite(id, 1, 32, f);
    fwrite(peer_pubkey, 1, 32, f);
    fwrite(shared_secret, 1, 32, f);
    fclose(f);
    return ZRTP_SUCCESS;
}

// Load trust entry (linear search). 加载信任条目（线性查找）。
zrtp_error_t zrtp_trust_store_load(const char* peer_id,
                                   uint8_t peer_pubkey[32],
                                   uint8_t shared_secret[32]) {
    if (!peer_id || !peer_pubkey || !shared_secret) {
        return ZRTP_ERROR_INVALID_ARGUMENT;
    }

    FILE* f = fopen(kTrustStoreFile, "rb");
    if (!f) return ZRTP_ERROR_TRUST_STORE_IO;

    char id[32];
    zrtp_error_t err = ZRTP_ERROR_TRUST_STORE_IO;
    while (fread(id, 1, 32, f) == 32) {
        if (strncmp(id, peer_id, 32) == 0) {
            if (fread(peer_pubkey, 1, 32, f) != 32) break;
            if (fread(shared_secret, 1, 32, f) != 32) break;
            err = ZRTP_SUCCESS;
            break;
        }
        // Skip this entry (pubkey+secret). 跳过该条目（公钥+秘密）。
        fseek(f, 64, SEEK_CUR);
    }
    fclose(f);
    return err;
}
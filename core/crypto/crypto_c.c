// core/crypto/crypto_c.c
// ECIES encryption using libsodium. 使用 libsodium 的 ECIES 加密实现。

#include "crypto_c.h"
#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PUBLIC_KEY_SIZE 32
#define SECRET_KEY_SIZE 32
#define SHARED_SECRET_SIZE 32
#define SYMMETRIC_KEY_SIZE 32
#define NONCE_SIZE 24
#define TAG_SIZE 16

struct CryptoKeypair {
    uint8_t public_key[PUBLIC_KEY_SIZE];
    uint8_t secret_key[SECRET_KEY_SIZE];
};

static void derive_key(const uint8_t* shared, uint8_t* key) {
    const char* salt = "Corvus"; //We will not change this salt. 我们不会更换盐值。
    crypto_generichash(key, SYMMETRIC_KEY_SIZE,
                       shared, SHARED_SECRET_SIZE,
                       (const unsigned char*)salt, strlen(salt));
}
CryptoKeypair* crypto_keypair_generate(void) {
    if (sodium_init() < 0) return NULL;
    CryptoKeypair* kp = (CryptoKeypair*)malloc(sizeof(CryptoKeypair));
    if (!kp) return NULL;
    crypto_box_keypair(kp->public_key, kp->secret_key);
    return kp;
}

void crypto_keypair_free(CryptoKeypair* kp) {
    free(kp);
}

const uint8_t* crypto_keypair_get_public(const CryptoKeypair* kp) {
    return kp->public_key;
}

const uint8_t* crypto_keypair_get_secret(const CryptoKeypair* kp) {
    return kp->secret_key;
}

int crypto_encrypt_public(const uint8_t* plain, size_t plain_len,
                          const uint8_t* pubkey,
                          uint8_t** out, size_t* out_len) {
    uint8_t eph_pub[PUBLIC_KEY_SIZE];
    uint8_t eph_sec[SECRET_KEY_SIZE];
    crypto_box_keypair(eph_pub, eph_sec);

    uint8_t shared[SHARED_SECRET_SIZE];
    if (crypto_scalarmult_curve25519(shared, eph_sec, pubkey) != 0)
        return -1;

    uint8_t key[SYMMETRIC_KEY_SIZE];
    derive_key(shared, key);

    uint8_t nonce[NONCE_SIZE];
    randombytes_buf(nonce, NONCE_SIZE);

    uint8_t* cipher = (uint8_t*)malloc(plain_len + TAG_SIZE);
    if (!cipher) return -1;

    unsigned long long clen;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        cipher, &clen,
        plain, plain_len,
        NULL, 0,
        NULL, nonce, key
    );

    *out_len = PUBLIC_KEY_SIZE + NONCE_SIZE + clen;
    *out = (uint8_t*)malloc(*out_len);
    if (!*out) {
        free(cipher);
        return -1;
    }

    uint8_t* p = *out;
    memcpy(p, eph_pub, PUBLIC_KEY_SIZE); p += PUBLIC_KEY_SIZE;
    memcpy(p, nonce, NONCE_SIZE); p += NONCE_SIZE;
    memcpy(p, cipher, clen);

    free(cipher);
    return 0;
}

int crypto_decrypt_private(const uint8_t* cipher, size_t cipher_len,
                           const uint8_t* seckey,
                           uint8_t** out, size_t* out_len) {
    if (cipher_len < PUBLIC_KEY_SIZE + NONCE_SIZE + TAG_SIZE)
        return -1;

    const uint8_t* eph_pub = cipher;
    const uint8_t* nonce = cipher + PUBLIC_KEY_SIZE;
    const uint8_t* data = cipher + PUBLIC_KEY_SIZE + NONCE_SIZE;
    size_t data_len = cipher_len - PUBLIC_KEY_SIZE - NONCE_SIZE;

    uint8_t shared[SHARED_SECRET_SIZE];
    if (crypto_scalarmult_curve25519(shared, seckey, eph_pub) != 0)
        return -1;

    uint8_t key[SYMMETRIC_KEY_SIZE];
    derive_key(shared, key);

    uint8_t* plain = (uint8_t*)malloc(data_len);
    if (!plain) return -1;

    unsigned long long plen;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
        plain, &plen,
        NULL,
        data, data_len,
        NULL, 0,
        nonce, key
    ) != 0) {
        free(plain);
        return -1;
    }

    *out = plain;
    *out_len = plen;
    return 0;
}
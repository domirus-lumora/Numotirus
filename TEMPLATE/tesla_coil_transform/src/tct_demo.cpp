// tct_demo.cpp
// 演示 + 100000 次随机往返测试

#include "tct.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <sodium.h>

int main() {
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return 1;
    }

    // 密钥
    uint64_t secret = 0x123456789ABCDEF0ULL;
    std::array<uint8_t, 32> key{};
    randombytes_buf(key.data(), key.size());

    tct::TeslaCoilTransform tct(secret, key);

    // 简单示例
    {
        uint8_t original = 42;
        int64_t max_val = 255;
        double plain = tct.scale_to_plaintext(original, max_val);
        tct::Nonce nonce{};
        randombytes_buf(nonce.data(), nonce.size());

        auto ct = tct.encrypt(plain, nonce);
        auto dec_opt = tct.decrypt(ct);
        if (dec_opt) {
            int64_t rec = tct.scale_from_plaintext(*dec_opt, max_val);
            std::cout << "Test: " << (int)original << " -> " << rec
                      << (rec == original ? " ✅\n" : " ❌\n");
        } else {
            std::cout << "Decryption failed\n";
        }
    }

    // 统计测试：100000 次随机往返
    std::cout << "\nRunning 100,000 random roundtrips...\n";
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::uniform_int_distribution<uint64_t> nonce_dist;

    int success = 0;
    int overflow = 0;
    double max_error = 0.0;
    double sum_error = 0.0;

    const int SAMPLES = 100000;
    for (int i = 0; i < SAMPLES; ++i) {
        double original = dist(rng);
        tct::Nonce nonce{};
        uint64_t nv = nonce_dist(rng);
        for (size_t j = 0; j < nonce.size(); ++j) {
            nonce[j] = (nv >> (j * 8)) & 0xFF;
        }

        auto ct = tct.encrypt(original, nonce);
        auto dec_opt = tct.decrypt(ct);

        if (!dec_opt) {
            ++overflow;
            continue;
        }
        double recovered = *dec_opt;
        double err = std::abs(original - recovered);
        if (err > max_error) max_error = err;
        sum_error += err;
        ++success;
    }

    double avg_error = (success > 0) ? sum_error / success : 0.0;

    std::cout << std::fixed << std::setprecision(8);
    std::cout << "Valid roundtrips: " << success << " / " << SAMPLES << "\n";
    std::cout << "Failures (NaN/overflow): " << overflow << "\n";
    std::cout << "Max error: " << max_error << "\n";
    std::cout << "Avg error: " << avg_error << "\n";

    return 0;
}
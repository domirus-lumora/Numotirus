// crypto_cli.cpp
// Numotirus command line encryption tool. Numotirus 命令行加密工具。
// Compile: g++ crypto.cpp crypto_cli.cpp -lsodium -o crypto_cli.exe
// Run: ./crypto_cli.exe

#include "crypto.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <clocale>

using namespace numotirus::crypto;

// Convert hex string to bytes. 十六进制字符串转字节数组。
std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// Convert bytes to hex string. 字节数组转十六进制字符串。
std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto b : bytes) {
        ss << std::setw(2) << (int)b;
    }
    return ss.str();
}

// Print separator line. 打印分隔线。
void print_sep() {
    std::cout << "\n----------------------------------------\n" << std::endl;
}

int main() {
    std::setlocale(LC_ALL, "zh_CN.UTF-8");
    system("chcp 65001 > nul");
    std::cout << "========================================" << std::endl;
    std::cout << "   Numotirus Command Line Crypto Tool" << std::endl;
    std::cout << "   Numotirus 命令行加密工具" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::string choice;
    while (true) {
        std::cout << "\nSelect operation / 请选择操作：" << std::endl;
        std::cout << "  1. Generate key pair / 生成密钥对" << std::endl;
        std::cout << "  2. Public key encrypt / 公钥加密" << std::endl;
        std::cout << "  3. Private key decrypt / 私钥解密" << std::endl;
        std::cout << "  4. Symmetric encrypt / 对称加密" << std::endl;
        std::cout << "  5. Symmetric decrypt / 对称解密" << std::endl;
        std::cout << "  0. Exit / 退出" << std::endl;
        std::cout << "Enter number / 输入数字: ";
        std::getline(std::cin, choice);
        
        if (choice == "0") {
            std::cout << "Goodbye! / 再见！" << std::endl;
            break;
        }
        
        print_sep();
        
        if (choice == "1") {
            // Generate key pair. 生成密钥对。
            auto kp = generate_keypair();
            std::cout << "Public key / 公钥:" << std::endl;
            std::cout << bytes_to_hex({kp.public_key.begin(), kp.public_key.end()}) << std::endl;
            std::cout << "\nSecret key / 私钥:" << std::endl;
            std::cout << bytes_to_hex({kp.secret.begin(), kp.secret.end()}) << std::endl;
            std::cout << "\nSave your secret key, do NOT share it!" << std::endl;
            std::cout << "请保存好私钥，不要给别人！" << std::endl;
            
        } else if (choice == "2") {
            // Public key encrypt. 公钥加密。
            std::string pubkey_hex, plaintext;
            std::cout << "Enter recipient's public key (hex) / 请输入对方的公钥（十六进制）: ";
            std::getline(std::cin, pubkey_hex);
            std::cout << "Enter message to encrypt / 请输入要加密的消息: ";
            std::getline(std::cin, plaintext);
            
            try {
                auto pubkey_bytes = hex_to_bytes(pubkey_hex);
                if (pubkey_bytes.size() != PUBLIC_KEY_SIZE) {
                    std::cout << "Error: public key must be " << PUBLIC_KEY_SIZE << " bytes" << std::endl;
                    std::cout << "错误：公钥长度应为 " << PUBLIC_KEY_SIZE << " 字节" << std::endl;
                    continue;
                }
                std::array<uint8_t, PUBLIC_KEY_SIZE> pubkey;
                std::copy(pubkey_bytes.begin(), pubkey_bytes.end(), pubkey.begin());
                
                std::vector<uint8_t> plain(plaintext.begin(), plaintext.end());
                auto cipher = encrypt_public(plain, pubkey);
                
                std::cout << "\nCiphertext (hex) / 密文（十六进制）:" << std::endl;
                std::cout << bytes_to_hex(cipher) << std::endl;
                std::cout << "\nCiphertext size / 密文长度: " << cipher.size() << " bytes / 字节" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Error / 错误: " << e.what() << std::endl;
            }
            
        } else if (choice == "3") {
            // Private key decrypt. 私钥解密。
            std::string privkey_hex, cipher_hex;
            std::cout << "Enter your secret key (hex) / 请输入你的私钥（十六进制）: ";
            std::getline(std::cin, privkey_hex);
            std::cout << "Enter ciphertext (hex) / 请输入密文（十六进制）: ";
            std::getline(std::cin, cipher_hex);
            
            try {
                auto privkey_bytes = hex_to_bytes(privkey_hex);
                if (privkey_bytes.size() != SECRET_KEY_SIZE) {
                    std::cout << "Error: secret key must be " << SECRET_KEY_SIZE << " bytes" << std::endl;
                    std::cout << "错误：私钥长度应为 " << SECRET_KEY_SIZE << " 字节" << std::endl;
                    continue;
                }
                std::array<uint8_t, SECRET_KEY_SIZE> privkey;
                std::copy(privkey_bytes.begin(), privkey_bytes.end(), privkey.begin());
                
                auto cipher = hex_to_bytes(cipher_hex);
                auto plain = decrypt_private(cipher, privkey);
                
                if (plain.empty()) {
                    std::cout << "Decryption failed! Wrong key or tampered ciphertext." << std::endl;
                    std::cout << "解密失败！可能是密钥错误或密文被篡改。" << std::endl;
                } else {
                    std::string message(plain.begin(), plain.end());
                    std::cout << "\nDecrypted message / 解密结果:" << std::endl;
                    std::cout << message << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error / 错误: " << e.what() << std::endl;
            }
            
        } else if (choice == "4") {
            // Symmetric encrypt. 对称加密。
            std::string key_hex, nonce_hex, plaintext;
            std::cout << "Enter symmetric key (32 bytes hex) / 请输入对称密钥（32字节，十六进制）: ";
            std::getline(std::cin, key_hex);
            std::cout << "Enter nonce (24 bytes hex) / 请输入随机数（24字节，十六进制）: ";
            std::getline(std::cin, nonce_hex);
            std::cout << "Enter message to encrypt / 请输入要加密的消息: ";
            std::getline(std::cin, plaintext);
            
            try {
                auto key_bytes = hex_to_bytes(key_hex);
                auto nonce_bytes = hex_to_bytes(nonce_hex);
                if (key_bytes.size() != KEY_SIZE) {
                    std::cout << "Error: key must be " << KEY_SIZE << " bytes" << std::endl;
                    std::cout << "错误：密钥长度应为 " << KEY_SIZE << " 字节" << std::endl;
                    continue;
                }
                if (nonce_bytes.size() != NONCE_SIZE) {
                    std::cout << "Error: nonce must be " << NONCE_SIZE << " bytes" << std::endl;
                    std::cout << "错误：随机数长度应为 " << NONCE_SIZE << " 字节" << std::endl;
                    continue;
                }
                std::array<uint8_t, KEY_SIZE> key;
                std::array<uint8_t, NONCE_SIZE> nonce;
                std::copy(key_bytes.begin(), key_bytes.end(), key.begin());
                std::copy(nonce_bytes.begin(), nonce_bytes.end(), nonce.begin());
                
                std::vector<uint8_t> plain(plaintext.begin(), plaintext.end());
                auto cipher = encrypt(plain, key, nonce);
                
                std::cout << "\nCiphertext (hex) / 密文（十六进制）:" << std::endl;
                std::cout << bytes_to_hex(cipher) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Error / 错误: " << e.what() << std::endl;
            }
            
        } else if (choice == "5") {
            // Symmetric decrypt. 对称解密。
            std::string key_hex, nonce_hex, cipher_hex;
            std::cout << "Enter symmetric key (32 bytes hex) / 请输入对称密钥（32字节，十六进制）: ";
            std::getline(std::cin, key_hex);
            std::cout << "Enter nonce (24 bytes hex) / 请输入随机数（24字节，十六进制）: ";
            std::getline(std::cin, nonce_hex);
            std::cout << "Enter ciphertext (hex) / 请输入密文（十六进制）: ";
            std::getline(std::cin, cipher_hex);
            
            try {
                auto key_bytes = hex_to_bytes(key_hex);
                auto nonce_bytes = hex_to_bytes(nonce_hex);
                if (key_bytes.size() != KEY_SIZE) {
                    std::cout << "Error: key must be " << KEY_SIZE << " bytes" << std::endl;
                    std::cout << "错误：密钥长度应为 " << KEY_SIZE << " 字节" << std::endl;
                    continue;
                }
                if (nonce_bytes.size() != NONCE_SIZE) {
                    std::cout << "Error: nonce must be " << NONCE_SIZE << " bytes" << std::endl;
                    std::cout << "错误：随机数长度应为 " << NONCE_SIZE << " 字节" << std::endl;
                    continue;
                }
                std::array<uint8_t, KEY_SIZE> key;
                std::array<uint8_t, NONCE_SIZE> nonce;
                std::copy(key_bytes.begin(), key_bytes.end(), key.begin());
                std::copy(nonce_bytes.begin(), nonce_bytes.end(), nonce.begin());
                
                auto cipher = hex_to_bytes(cipher_hex);
                auto plain = decrypt(cipher, key, nonce);
                
                if (plain.empty()) {
                    std::cout << "Decryption failed! Wrong key or tampered ciphertext." << std::endl;
                    std::cout << "解密失败！密钥错误或密文被篡改。" << std::endl;
                } else {
                    std::string message(plain.begin(), plain.end());
                    std::cout << "\nDecrypted message / 解密结果:" << std::endl;
                    std::cout << message << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error / 错误: " << e.what() << std::endl;
            }
        }
        
        print_sep();
    }
    
    return 0;
}
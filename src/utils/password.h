#pragma once

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace PasswordUtil {

inline std::string generateSalt(int length = 32) {
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

  std::string salt;
  salt.reserve(length);
  for (int i = 0; i < length; i++) {
    salt += charset[dist(gen)];
  }
  return salt;
}

inline std::string sha256(const std::string &input) {
#ifdef __APPLE__
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(input.c_str(), static_cast<CC_LONG>(input.length()), hash);

  std::stringstream ss;
  for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
    ss << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<int>(hash[i]);
  }
#else
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
  EVP_DigestUpdate(mdctx, input.c_str(), input.length());
  EVP_DigestFinal_ex(mdctx, hash, &hash_len);
  EVP_MD_CTX_free(mdctx);

  std::stringstream ss;
  for (unsigned int i = 0; i < hash_len; i++) {
    ss << std::hex << std::setfill('0') << std::setw(2)
       << static_cast<int>(hash[i]);
  }
#endif
  return ss.str();
}

inline std::string hashPassword(const std::string &password,
                                const std::string &salt) {
  // Multiple rounds of SHA256 for basic strengthening
  std::string hash = password + salt;
  for (int i = 0; i < 10000; i++) {
    hash = sha256(hash + salt);
  }
  return hash;
}

inline bool verifyPassword(const std::string &password, const std::string &hash,
                           const std::string &salt) {
  return hashPassword(password, salt) == hash;
}

} // namespace PasswordUtil

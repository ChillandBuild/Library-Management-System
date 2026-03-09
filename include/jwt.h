#pragma once

#include "json.hpp"
#ifdef __APPLE__
#include <CommonCrypto/CommonHMAC.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace JWT {

// Simple Base64URL encoding/decoding
inline std::string base64url_encode(const std::string &input) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  int val = 0, valb = -6;
  for (unsigned char c : input) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      encoded.push_back(table[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    encoded.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
  // No padding for URL-safe
  // Replace + with - and / with _
  for (auto &c : encoded) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  return encoded;
}

inline std::string base64url_decode(const std::string &input) {
  std::string in = input;
  for (auto &c : in) {
    if (c == '-')
      c = '+';
    else if (c == '_')
      c = '/';
  }
  // Add padding
  while (in.size() % 4)
    in.push_back('=');

  static const int lookup[] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
      -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
      -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

  std::string decoded;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (c == '=')
      break;
    if (c >= 128 || lookup[c] == -1)
      continue;
    val = (val << 6) + lookup[c];
    valb += 6;
    if (valb >= 0) {
      decoded.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return decoded;
}

inline std::string hmac_sha256(const std::string &key,
                               const std::string &data) {
#ifdef __APPLE__
  unsigned char result[CC_SHA256_DIGEST_LENGTH];
  CCHmac(kCCHmacAlgSHA256, key.c_str(), key.length(), data.c_str(),
         data.length(), result);
  return std::string(reinterpret_cast<char *>(result), CC_SHA256_DIGEST_LENGTH);
#else
  unsigned int result_len = EVP_MAX_MD_SIZE;
  unsigned char result[EVP_MAX_MD_SIZE];
  HMAC(EVP_sha256(), key.c_str(), key.length(),
       (const unsigned char *)data.c_str(), data.length(), result, &result_len);
  return std::string(reinterpret_cast<char *>(result), result_len);
#endif
}

const std::string SECRET_KEY = "lms_jwt_secret_key_change_in_production_2024";

inline std::string createToken(int userId, const std::string &username,
                               const std::string &role, int expiryHours = 24) {
  // Header
  json header = {{"alg", "HS256"}, {"typ", "JWT"}};
  std::string headerEncoded = base64url_encode(header.dump());

  // Payload
  auto now = std::chrono::system_clock::now();
  auto exp = now + std::chrono::hours(expiryHours);
  auto nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();
  auto expSec =
      std::chrono::duration_cast<std::chrono::seconds>(exp.time_since_epoch())
          .count();

  json payload = {{"user_id", userId},
                  {"username", username},
                  {"role", role},
                  {"iat", nowSec},
                  {"exp", expSec}};
  std::string payloadEncoded = base64url_encode(payload.dump());

  // Signature
  std::string signInput = headerEncoded + "." + payloadEncoded;
  std::string signature = base64url_encode(hmac_sha256(SECRET_KEY, signInput));

  return signInput + "." + signature;
}

inline json verifyToken(const std::string &token) {
  // Split token
  std::vector<std::string> parts;
  std::stringstream ss(token);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }
  if (parts.size() != 3)
    return nullptr;

  // Verify signature
  std::string signInput = parts[0] + "." + parts[1];
  std::string expectedSig =
      base64url_encode(hmac_sha256(SECRET_KEY, signInput));

  if (expectedSig != parts[2])
    return nullptr;

  // Decode payload
  std::string payloadStr = base64url_decode(parts[1]);
  json payload;
  try {
    payload = json::parse(payloadStr);
  } catch (...) {
    return nullptr;
  }

  // Check expiry
  auto now = std::chrono::system_clock::now();
  auto nowSec =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();
  if (payload.contains("exp") && payload["exp"].get<int64_t>() < nowSec) {
    return nullptr; // Expired
  }

  return payload;
}

} // namespace JWT

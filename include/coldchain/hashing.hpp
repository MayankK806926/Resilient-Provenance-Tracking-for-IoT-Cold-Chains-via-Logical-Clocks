#pragma once
/// @file hashing.hpp
/// @brief SHA-256 hashing and HMAC/digital signature verification.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace coldchain {

using Hash256 = std::array<uint8_t, 32>;

Hash256 sha256_bytes(std::string_view data);
std::string sha256_hex(std::string_view data);
std::string hash_to_hex(const Hash256& hash);

Hash256 sign_event(std::string_view serialized_event, std::string_view node_secret);
bool verify_signature(std::string_view serialized_event,
                      std::string_view node_secret,
                      const Hash256& expected_sig);

// Distinct per-node secret keys (replaces static default_node_secret)
std::string node_private_key(uint8_t entity_id);
std::string default_node_secret(uint8_t entity_id);

} // namespace coldchain

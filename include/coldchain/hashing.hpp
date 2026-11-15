#pragma once
/// @file hashing.hpp
/// @brief SHA-256 hashing and simulated MAC signatures.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "coldchain/events.hpp"

namespace coldchain {

Hash256 sha256_bytes(std::string_view data);
std::string sha256_hex(std::string_view data);

Hash256 sign_event(std::string_view serialized_event, std::string_view node_secret);
bool verify_signature(std::string_view serialized_event,
                      std::string_view node_secret,
                      const Hash256& expected_sig);

std::string default_node_secret(uint8_t entity_id);

} // namespace coldchain

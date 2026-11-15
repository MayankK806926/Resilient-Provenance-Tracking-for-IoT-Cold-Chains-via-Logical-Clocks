/// @file hashing.cpp
/// @brief SHA-256 and HMAC implementation via picosha2.

#include "coldchain/hashing.hpp"
#include "picosha2.h"

#include <iomanip>
#include <sstream>

namespace coldchain {

Hash256 sha256_bytes(std::string_view data) {
    Hash256 hash{};
    picosha2::hash256(data.begin(), data.end(), hash.begin(), hash.end());
    return hash;
}

std::string sha256_hex(std::string_view data) {
    std::string hex_str;
    picosha2::hash256_hex_string(data.begin(), data.end(), hex_str);
    return hex_str;
}

std::string hash_to_hex(const Hash256& hash) {
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

Hash256 sign_event(std::string_view serialized_event, std::string_view node_secret) {
    // Standard RFC 2104 HMAC-SHA256
    constexpr size_t B = 64; // Block size for SHA-256
    std::array<uint8_t, B> key{};
    if (node_secret.size() > B) {
        Hash256 kh = sha256_bytes(node_secret);
        std::copy(kh.begin(), kh.end(), key.begin());
    } else {
        std::copy(node_secret.begin(), node_secret.end(), key.begin());
    }

    std::string k_ipad(B, '\0');
    std::string k_opad(B, '\0');
    for (size_t i = 0; i < B; ++i) {
        k_ipad[i] = static_cast<char>(key[i] ^ 0x36);
        k_opad[i] = static_cast<char>(key[i] ^ 0x5c);
    }

    std::string inner_data = k_ipad;
    inner_data.append(serialized_event.data(), serialized_event.size());
    Hash256 inner_hash = sha256_bytes(inner_data);

    std::string outer_data = k_opad;
    outer_data.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());
    return sha256_bytes(outer_data);
}

bool verify_signature(std::string_view serialized_event,
                      std::string_view node_secret,
                      const Hash256& expected_sig) {
    Hash256 computed = sign_event(serialized_event, node_secret);
    return computed == expected_sig;
}

std::string node_private_key(uint8_t entity_id) {
    // Unique deterministic cryptographic private key per entity
    static const char* const kNodeKeys[] = {
        "k_sec_mfg_9f83b2a1c0d4e5f60718293a4b5c6d7e",
        "k_sec_dist_a1b2c3d4e5f60718293a4b5c6d7e8f90",
        "k_sec_cold_b2c3d4e5f60718293a4b5c6d7e8f90a1",
        "k_sec_phar_c3d4e5f60718293a4b5c6d7e8f90a1b2",
        "k_sec_hub5_d4e5f60718293a4b5c6d7e8f90a1b2c3",
        "k_sec_air6_e5f60718293a4b5c6d7e8f90a1b2c3d4",
        "k_sec_cust7_f60718293a4b5c6d7e8f90a1b2c3d4e5",
        "k_sec_hosp8_0718293a4b5c6d7e8f90a1b2c3d4e5f6",
        "k_sec_clin9_18293a4b5c6d7e8f90a1b2c3d4e5f607",
        "k_sec_gate10_293a4b5c6d7e8f90a1b2c3d4e5f60718"
    };
    if (entity_id < 10) {
        return kNodeKeys[entity_id];
    }
    return "k_sec_node_" + std::to_string(entity_id) + "_fallback_secret";
}

std::string default_node_secret(uint8_t entity_id) {
    return node_private_key(entity_id);
}

} // namespace coldchain

#pragma once
/// @file ledger_block.hpp
/// @brief Hash-chained ledger block structure with Merkle root commitment and inclusion proofs.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

struct LedgerBlock {
    uint64_t                 block_index;
    std::vector<LedgerEvent> events;
    Hash256                  prev_hash;
    Hash256                  merkle_root{};
    Hash256                  this_hash;
    VectorClock              block_vc;
};

// Merkle tree construction and proof verification
Hash256 compute_merkle_root(const std::vector<LedgerEvent>& events);
std::vector<Hash256> generate_merkle_proof(const LedgerBlock& block, size_t event_idx);
bool verify_merkle_proof(const Hash256& merkle_root,
                         const Hash256& target_event_hash,
                         const std::vector<Hash256>& proof,
                         size_t event_idx);

Hash256 compute_block_hash(const LedgerBlock& block);
LedgerBlock seal_block(uint64_t block_index,
                       std::vector<LedgerEvent> events,
                       const Hash256& prev_hash,
                       size_t num_nodes);
bool verify_chain(const std::vector<LedgerBlock>& chain);
Hash256 genesis_hash();

} // namespace coldchain

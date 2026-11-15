#pragma once
/// @file ledger_block.hpp
/// @brief Hash-chained ledger block structure and operations.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

struct LedgerBlock {
    uint64_t                 block_index;
    std::vector<LedgerEvent> events;
    Hash256                  prev_hash;
    Hash256                  this_hash;
    VectorClock              block_vc;
};

Hash256 compute_block_hash(const LedgerBlock& block);
LedgerBlock seal_block(uint64_t block_index,
                       std::vector<LedgerEvent> events,
                       const Hash256& prev_hash,
                       size_t num_nodes);
bool verify_chain(const std::vector<LedgerBlock>& chain);
Hash256 genesis_hash();

} // namespace coldchain

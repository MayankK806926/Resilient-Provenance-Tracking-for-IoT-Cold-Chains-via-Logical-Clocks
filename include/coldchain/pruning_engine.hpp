#pragma once
/// @file pruning_engine.hpp
/// @brief Garbage collection of stable state store records via matrix clock horizons.

#include <cstddef>
#include <vector>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

class LedgerNode;

class PruningEngine {
public:
    static size_t prune_stable_records(LedgerNode& node);
    static bool is_safe_to_prune(const VectorClock& event_vc,
                                 const VectorClock& horizon);
    static size_t count_prunable(const LedgerNode& node);
    static VectorClock pruning_horizon(const LedgerNode& node);
};

} // namespace coldchain

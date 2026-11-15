#pragma once
/// @file pruning_engine.hpp
/// @brief Matrix-clock-driven state store and payload pruning with safety invariant verification.

#include <cstddef>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class LedgerNode;

class PruningEngine {
public:
    // Core pruning: drops stable normal readings from state store and frees payload in chain
    static size_t prune_stable_records(LedgerNode& node,
                                       float temp_low_c = 2.0f,
                                       float temp_high_c = 8.0f,
                                       const std::vector<LedgerNode>* all_nodes_oracle = nullptr,
                                       size_t* out_safety_violations = nullptr);

    // Pruning safety predicate: event_vc <= horizon across all dimensions
    static bool is_safe_to_prune(const VectorClock& event_vc, const VectorClock& horizon);

    // Retention policy: excursions must NEVER be pruned
    static bool is_excursion(const SensorEvent& ev, float temp_low_c, float temp_high_c);

    // Diagnostics & horizons
    static size_t count_prunable(const LedgerNode& node, float temp_low_c = 2.0f, float temp_high_c = 8.0f);
    static VectorClock pruning_horizon(const LedgerNode& node);
};

} // namespace coldchain

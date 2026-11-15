#pragma once
/// @file causal_merge_engine.hpp
/// @brief Kahn's topological sort over vector clock DAG with deterministic hash tie-breaking.

#include <functional>
#include <string>
#include <vector>

#include "coldchain/events.hpp"

namespace coldchain {

class CausalMergeEngine {
public:
    // Topological sort with deterministic tie-breaking on SHA-256 event hash
    static std::vector<LedgerEvent> topo_sort(std::vector<LedgerEvent> incoming);

    // Cryptographic signature and membership verification
    static bool verify_event(const LedgerEvent& ev,
                             size_t num_registered_nodes,
                             const std::function<std::string(uint8_t)>& secret_fn = {});

    static bool verify_signatures(const std::vector<LedgerEvent>& batch,
                                  size_t num_registered_nodes,
                                  const std::function<std::string(uint8_t)>& secret_fn = {});

    static bool is_causally_valid(const std::vector<LedgerEvent>& batch);
};

} // namespace coldchain

#pragma once
/// @file causal_merge_engine.hpp
/// @brief Kahn's topological sort over vector clock DAG with deterministic tie-breaking.

#include <functional>
#include <string>
#include <vector>

#include "coldchain/events.hpp"

namespace coldchain {

class CausalMergeEngine {
public:
    static std::vector<LedgerEvent> topo_sort(std::vector<LedgerEvent> incoming);
    static bool verify_signatures(const std::vector<LedgerEvent>& batch,
                                  const std::function<std::string(uint8_t)>& secret_fn = {});
    static bool is_causally_valid(const std::vector<LedgerEvent>& batch);
};

} // namespace coldchain

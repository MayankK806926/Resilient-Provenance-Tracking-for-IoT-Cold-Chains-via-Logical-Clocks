/// @file pruning_engine.cpp
/// @brief PruningEngine implementation — matrix-clock-driven garbage collection and safety oracle.

#include "coldchain/pruning_engine.hpp"
#include "coldchain/ledger_node.hpp"

#include <cassert>
#include <iostream>
#include <set>

namespace coldchain {

bool PruningEngine::is_safe_to_prune(const VectorClock& event_vc,
                                     const VectorClock& horizon) {
    assert(event_vc.size() == horizon.size());
    for (size_t k = 0; k < event_vc.size(); ++k) {
        if (horizon[k] < event_vc[k]) {
            return false;
        }
    }
    return true;
}

bool PruningEngine::is_excursion(const SensorEvent& ev, float temp_low_c, float temp_high_c) {
    return ev.temperature_celsius < temp_low_c || ev.temperature_celsius > temp_high_c;
}

size_t PruningEngine::prune_stable_records(LedgerNode& node,
                                           float temp_low_c,
                                           float temp_high_c,
                                           const std::vector<LedgerNode>* all_nodes_oracle,
                                           size_t* out_safety_violations) {
    VectorClock horizon = node.matrix_clock().column_min();
    auto& store = node.active_state_store_mut();

    size_t removed = 0;
    std::vector<Hash256> pruned_hashes;

    for (auto it = store.begin(); it != store.end(); ) {
        const auto& ev = it->second;

        // Retention policy: Never prune excursions or custody events
        if (is_excursion(ev, temp_low_c, temp_high_c)) {
            ++it;
            continue;
        }

        if (is_safe_to_prune(ev.vc, horizon)) {
            // Safety invariant check via oracle: verify every node has actually delivered this event
            if (all_nodes_oracle) {
                Hash256 eid = ev.id != Hash256{} ? ev.id : ev.compute_hash();
                for (const auto& peer : *all_nodes_oracle) {
                    if (!peer.has_delivered(eid)) {
                        if (out_safety_violations) {
                            ++(*out_safety_violations);
                        }
                    }
                }
            }

            pruned_hashes.push_back(ev.id);
            it = store.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    // Prune payload from all_events_ history & chain while retaining hashes & Merkle commitments (O(N log M))
    if (!pruned_hashes.empty()) {
        std::set<Hash256> pruned_set(pruned_hashes.begin(), pruned_hashes.end());

        auto& all_evs = node.all_events_mut();
        for (auto& ev : all_evs) {
            if (std::holds_alternative<SensorEvent>(ev)) {
                if (pruned_set.count(event_hash(ev))) {
                    prune_event_payload(ev);
                }
            }
        }

        // Also prune payload in sealed blocks while maintaining Merkle tree proofs
        for (auto& blk : node.chain_mut()) {
            for (auto& ev : blk.events) {
                if (std::holds_alternative<SensorEvent>(ev)) {
                    if (pruned_set.count(event_hash(ev))) {
                        prune_event_payload(ev);
                    }
                }
            }
        }
    }

    return removed;
}

size_t PruningEngine::count_prunable(const LedgerNode& node, float temp_low_c, float temp_high_c) {
    VectorClock horizon = node.matrix_clock().column_min();
    const auto& store = node.active_state_store();

    size_t count = 0;
    for (const auto& [key, event] : store) {
        if (!is_excursion(event, temp_low_c, temp_high_c) &&
            is_safe_to_prune(event.vc, horizon)) {
            ++count;
        }
    }
    return count;
}

VectorClock PruningEngine::pruning_horizon(const LedgerNode& node) {
    return node.matrix_clock().column_min();
}

} // namespace coldchain

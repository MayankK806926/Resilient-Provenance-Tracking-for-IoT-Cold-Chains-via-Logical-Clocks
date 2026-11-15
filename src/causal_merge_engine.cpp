/// @file causal_merge_engine.cpp
/// @brief Implementation of Kahn's topological sort over vector clock partial orders
///        with deterministic event-hash tie breaking and signature verification.

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/hashing.hpp"

#include <queue>
#include <stdexcept>

namespace coldchain {

namespace {

struct DeterministicTieBreaker {
    const std::vector<LedgerEvent>* events;
    const std::vector<Hash256>*     hashes;

    bool operator()(size_t a, size_t b) const {
        uint8_t orig_a = event_origin((*events)[a]);
        uint8_t orig_b = event_origin((*events)[b]);
        if (orig_a != orig_b) return orig_a > orig_b;

        uint64_t cnt_a = event_counter((*events)[a]);
        uint64_t cnt_b = event_counter((*events)[b]);
        if (cnt_a != cnt_b) return cnt_a > cnt_b;

        // Canonical tie-breaker: compare 256-bit event hash lexicographically
        // (independent of arrival order or replica input permutation)
        return (*hashes)[a] > (*hashes)[b];
    }
};

} // namespace

std::vector<LedgerEvent> CausalMergeEngine::topo_sort(std::vector<LedgerEvent> incoming) {
    const size_t n = incoming.size();
    if (n <= 1) return incoming;

    // Pre-extract vector clocks and event hashes to optimize O(n^2) comparisons
    std::vector<const VectorClock*> vcs(n);
    std::vector<Hash256> hashes(n);
    std::vector<uint32_t> sums(n);

    for (size_t i = 0; i < n; ++i) {
        vcs[i]    = &event_vc(incoming[i]);
        hashes[i] = event_hash(incoming[i]);
        sums[i]   = vcs[i]->sum();
    }

    // 1. Build adjacency list for strict happens-before relation: e_i -> e_j
    std::vector<std::vector<size_t>> adj(n);
    std::vector<size_t> in_degree(n, 0);

    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            // Prune: vc_i can only precede vc_j if sum_i < sums[j]
            if (sum_i < sums[j] && vc_i.precedes(*vcs[j])) {
                adj[i].push_back(j);
                ++in_degree[j];
            }
        }
    }

    // 2. Priority queue for in-degree-0 nodes with deterministic hash tie-breaker
    DeterministicTieBreaker comp{&incoming, &hashes};
    std::priority_queue<size_t, std::vector<size_t>, DeterministicTieBreaker> pq(comp);

    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            pq.push(i);
        }
    }

    // 3. Process nodes via Kahn's algorithm
    std::vector<LedgerEvent> sorted;
    sorted.reserve(n);

    while (!pq.empty()) {
        size_t curr = pq.top();
        pq.pop();
        sorted.push_back(incoming[curr]);

        for (size_t succ : adj[curr]) {
            --in_degree[succ];
            if (in_degree[succ] == 0) {
                pq.push(succ);
            }
        }
    }

    // Explicit cycle check (NOT compiled out in Release mode)
    if (sorted.size() != n) {
        throw std::runtime_error("CausalMergeEngine: cycle detected in vector clock DAG!");
    }

    return sorted;
}

bool CausalMergeEngine::verify_event(const LedgerEvent& ev,
                                     size_t num_registered_nodes,
                                     const std::function<std::string(uint8_t)>& secret_fn) {
    uint8_t origin = event_origin(ev);

    // Membership check: reject unregistered entities
    if (origin >= num_registered_nodes) {
        return false;
    }

    std::string secret = secret_fn ? secret_fn(origin) : node_private_key(origin);

    return std::visit([&](const auto& e) -> bool {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, SensorEvent>) {
            return verify_signature(e.serialize(), secret, e.signature);
        } else {
            // Custody events carry sender signature
            return verify_signature(e.serialize(), secret, e.transfer_signature);
        }
    }, ev);
}

bool CausalMergeEngine::verify_signatures(const std::vector<LedgerEvent>& batch,
                                          size_t num_registered_nodes,
                                          const std::function<std::string(uint8_t)>& secret_fn) {
    for (const auto& ev : batch) {
        if (!verify_event(ev, num_registered_nodes, secret_fn)) {
            return false;
        }
    }
    return true;
}

bool CausalMergeEngine::is_causally_valid(const std::vector<LedgerEvent>& batch) {
    const size_t n = batch.size();
    if (n <= 1) return true;

    std::vector<const VectorClock*> vcs(n);
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(batch[i]);
        sums[i] = vcs[i]->sum();
    }
    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = i + 1; j < n; ++j) {
            if (sums[j] < sum_i && vcs[j]->precedes(vc_i)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace coldchain

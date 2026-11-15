#pragma once
/// @file ledger_node.hpp
/// @brief Replicated permissioned ledger node with causal hold-back queue, auto-sealing, and Merkle tree.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class LedgerNode {
public:
    explicit LedgerNode(uint8_t node_id, size_t n_nodes, size_t events_per_block = 32, size_t num_shipments = 1);

    // Ingest single event or batch through signature verification, topo-sort, and hold-back queue
    bool ingest_event(LedgerEvent ev, bool verify_sig = true, bool use_holdback = true);
    bool ingest_batch(std::vector<LedgerEvent> batch, bool verify_sig = true, bool use_holdback = true);

    // Block sealing
    LedgerBlock seal_block();
    size_t auto_seal();

    // Replicated convergence check
    Hash256 canonical_state_hash() const;
    size_t delivered_count() const;
    bool has_delivered(const Hash256& event_id) const;

    // Chain and event accessors
    const std::vector<LedgerBlock>& chain() const;
    std::vector<LedgerBlock>&       chain_mut();
    size_t pending_count() const;
    size_t hold_back_count() const;
    const std::vector<LedgerEvent>& hold_back_queue() const;
    const std::vector<LedgerEvent>& all_events() const;
    std::vector<LedgerEvent>& all_events_mut();

    // Working state store
    const std::map<uint64_t, SensorEvent>& active_state_store() const;
    std::map<uint64_t, SensorEvent>& active_state_store_mut();

    // Clocks
    const VectorClock& local_clock() const;
    MatrixClock&       matrix_clock();
    const MatrixClock& matrix_clock() const;
    MatrixClock&       shipment_matrix_clock(uint32_t sid);
    const MatrixClock& shipment_matrix_clock(uint32_t sid) const;
    const std::map<uint32_t, MatrixClock>& all_shipment_matrix_clocks() const;
    const VectorClock& shipment_delivered_vc(uint32_t sid) const;

    uint8_t node_id() const;
    size_t  events_per_block() const;
    size_t  total_sensor_events_delivered() const;

    // Memory footprint estimates
    size_t active_store_bytes() const;
    size_t total_memory_bytes() const;

private:
    bool is_causally_ready(const LedgerEvent& ev) const;
    void deliver_event(LedgerEvent ev);
    void drain_hold_back_queue();

    uint8_t node_id_;
    size_t  n_nodes_;
    size_t  events_per_block_;

    VectorClock local_vc_;
    MatrixClock mc_;

    std::vector<LedgerBlock> chain_;
    std::vector<LedgerEvent> pending_;
    std::vector<LedgerEvent> all_events_;
    std::vector<LedgerEvent> hold_back_queue_;

    std::set<Hash256> delivered_hashes_;
    size_t num_shipments_ = 1;
    std::map<uint32_t, MatrixClock> shipment_mcs_;
    std::map<uint32_t, VectorClock> delivered_vcs_;
    std::map<uint64_t, SensorEvent> active_state_store_;
    uint64_t next_state_key_ = 0;
    size_t total_sensor_events_delivered_ = 0;
};

} // namespace coldchain

#pragma once
/// @file ledger_node.hpp
/// @brief Entity ledger node maintaining local chain, working state store, and matrix clock.

#include <cstdint>
#include <map>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class LedgerNode {
public:
    LedgerNode(uint8_t node_id, size_t n_nodes);

    bool ingest_batch(std::vector<LedgerEvent> sorted_batch);
    LedgerBlock seal_block();
    size_t auto_seal(size_t threshold = 32);

    const std::vector<LedgerBlock>& chain() const;
    size_t pending_count() const;
    const std::vector<LedgerEvent>& all_events() const;

    const std::map<uint64_t, SensorEvent>& active_state_store() const;
    std::map<uint64_t, SensorEvent>& active_state_store_mut();

    MatrixClock& matrix_clock();
    const MatrixClock& matrix_clock() const;

    uint8_t node_id() const;

private:
    uint8_t node_id_;
    size_t  n_nodes_;
    std::vector<LedgerBlock> chain_;
    std::vector<LedgerEvent> pending_;
    std::vector<LedgerEvent> all_events_;
    std::map<uint64_t, SensorEvent> active_state_store_;
    MatrixClock mc_;
    uint64_t next_state_key_ = 0;
};

} // namespace coldchain

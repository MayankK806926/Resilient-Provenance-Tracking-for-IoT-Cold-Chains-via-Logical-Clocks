/// @file ledger_node.cpp
/// @brief Replicated permissioned ledger node implementation with causal hold-back queue.

#include "coldchain/ledger_node.hpp"
#include "coldchain/causal_merge_engine.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace coldchain {

LedgerNode::LedgerNode(uint8_t node_id, size_t n_nodes, size_t events_per_block, size_t num_shipments)
    : node_id_(node_id),
      n_nodes_(n_nodes),
      events_per_block_(events_per_block),
      local_vc_(n_nodes),
      mc_(n_nodes),
      num_shipments_(num_shipments) {
    for (size_t s = 1; s <= num_shipments_; ++s) {
        shipment_vcs_[static_cast<uint32_t>(s)] = VectorClock(n_nodes_);
    }
}

bool LedgerNode::is_causally_ready(const LedgerEvent& ev) const {
    if (const auto* se = std::get_if<SensorEvent>(&ev)) {
        // Gated on previous custody handoff: reading belongs to leg after handoff se->custody_transfer_seq
        if (se->custody_transfer_seq > 0) {
            uint64_t delivered_custody = 0;
            auto cit = custody_sequences_.find(se->shipment_id);
            if (cit != custody_sequences_.end()) {
                delivered_custody = cit->second;
            }
            if (delivered_custody < se->custody_transfer_seq) {
                return false; // Waiting for preceding custody handoff
            }
        }

        // Gated on monotonic sensor counter for this shipment
        uint64_t expected_counter = 1;
        auto it = shipment_counters_.find(se->shipment_id);
        if (it != shipment_counters_.end()) {
            expected_counter = it->second + 1;
        }
        if (se->local_monotonic_counter > expected_counter) {
            return false; // Gap in causal sequence for this shipment — hold back
        }
    } else if (const auto* ce = std::get_if<CustodyEvent>(&ev)) {
        // Gated on per-shipment custody sequence (1, 2, 3...)
        uint64_t expected_seq = 1;
        auto it = custody_sequences_.find(ce->shipment_id);
        if (it != custody_sequences_.end()) {
            expected_seq = it->second + 1;
        }
        if (ce->custody_transfer_seq > expected_seq) {
            return false; // Gap in custody handoffs for this shipment — hold back
        }

        // Gated on sender's prior sensor readings: all readings up to last_sensor_seq must be delivered!
        auto sit = shipment_counters_.find(ce->shipment_id);
        uint64_t delivered_sensor = (sit != shipment_counters_.end()) ? sit->second : 0;
        if (delivered_sensor < ce->last_sensor_seq) {
            return false; // Custody cannot precede sensor readings taken under prior custodian!
        }
    }

    return true;
}

void LedgerNode::deliver_event(LedgerEvent ev) {
    Hash256 h = event_hash(ev);
    if (delivered_hashes_.count(h)) {
        return; // Idempotent deduplication
    }

    delivered_hashes_.insert(h);
    local_vc_.merge(event_vc(ev));

    uint32_t sid = event_shipment_id(ev);
    if (sid > 0) {
        auto it = shipment_vcs_.find(sid);
        if (it == shipment_vcs_.end()) {
            shipment_vcs_[sid] = VectorClock(n_nodes_);
        }
        shipment_vcs_[sid].merge(event_vc(ev));
    }

    // Conservative stable progress across active shipments
    VectorClock stable_vc(n_nodes_);
    for (size_t k = 0; k < n_nodes_; ++k) {
        uint32_t min_val = UINT32_MAX;
        if (shipment_vcs_.empty()) {
            min_val = local_vc_[k];
        } else {
            for (const auto& [id, vc] : shipment_vcs_) {
                min_val = std::min(min_val, vc[k]);
            }
        }
        stable_vc.set(k, (min_val == UINT32_MAX) ? 0 : min_val);
    }

    // Update own row in matrix clock to reflect local delivery
    mc_.set_own_row(stable_vc, node_id_);

    // Update causal sequence counters
    if (const auto* se = std::get_if<SensorEvent>(&ev)) {
        shipment_counters_[se->shipment_id] = std::max(
            shipment_counters_[se->shipment_id], se->local_monotonic_counter);
        active_state_store_[next_state_key_++] = *se;
    } else if (const auto* ce = std::get_if<CustodyEvent>(&ev)) {
        custody_sequences_[ce->shipment_id] = std::max(
            custody_sequences_[ce->shipment_id], ce->custody_transfer_seq);
    }

    all_events_.push_back(ev);
    pending_.push_back(std::move(ev));

    // Auto-seal multi-block chain if threshold reached
    if (pending_.size() >= events_per_block_) {
        auto_seal();
    }
}

void LedgerNode::drain_hold_back_queue() {
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto it = hold_back_queue_.begin(); it != hold_back_queue_.end(); ) {
            if (is_causally_ready(*it)) {
                LedgerEvent ev = std::move(*it);
                it = hold_back_queue_.erase(it);
                deliver_event(std::move(ev));
                progress = true;
            } else {
                ++it;
            }
        }
    }
}

bool LedgerNode::ingest_event(LedgerEvent ev, bool verify_sig, bool use_holdback) {
    // 1. Signature & membership check
    if (verify_sig) {
        if (!CausalMergeEngine::verify_event(ev, n_nodes_)) {
            return false;
        }
    }

    // 2. Idempotent deduplication
    Hash256 h = event_hash(ev);
    if (delivered_hashes_.count(h)) {
        return true; // Already delivered
    }

    // 3. Causal delivery via hold-back queue
    if (use_holdback) {
        if (is_causally_ready(ev)) {
            deliver_event(std::move(ev));
            drain_hold_back_queue();
        } else {
            hold_back_queue_.push_back(std::move(ev));
        }
    } else {
        // Direct ingestion (for ablation evaluation)
        deliver_event(std::move(ev));
    }

    return true;
}

bool LedgerNode::ingest_batch(std::vector<LedgerEvent> batch, bool verify_sig, bool use_holdback) {
    if (batch.empty()) return true;

    // 1. Signature & membership verification on the entire batch
    if (verify_sig) {
        if (!CausalMergeEngine::verify_signatures(batch, n_nodes_)) {
            return false;
        }
    }

    // 2. Filter out already-delivered duplicates
    std::vector<LedgerEvent> fresh;
    fresh.reserve(batch.size());
    for (auto& ev : batch) {
        if (delivered_hashes_.find(event_hash(ev)) == delivered_hashes_.end()) {
            fresh.push_back(std::move(ev));
        }
    }
    if (fresh.empty()) return true;

    // 3. Topologically sort the fresh batch deterministically
    auto sorted = CausalMergeEngine::topo_sort(std::move(fresh));

    // 4. Merge each event through the hold-back queue
    for (auto& ev : sorted) {
        ingest_event(std::move(ev), /*verify_sig*/ false, use_holdback);
    }

    return true;
}

LedgerBlock LedgerNode::seal_block() {
    assert(!pending_.empty() && "seal_block: no pending events");

    Hash256 prev = chain_.empty() ? genesis_hash() : chain_.back().this_hash;
    auto blk = coldchain::seal_block(chain_.size(), std::move(pending_), prev, n_nodes_);
    pending_.clear();
    chain_.push_back(blk);
    return blk;
}

size_t LedgerNode::auto_seal() {
    size_t sealed = 0;
    while (pending_.size() >= events_per_block_) {
        std::vector<LedgerEvent> batch(pending_.begin(), pending_.begin() + events_per_block_);
        pending_.erase(pending_.begin(), pending_.begin() + events_per_block_);

        Hash256 prev = chain_.empty() ? genesis_hash() : chain_.back().this_hash;
        auto blk = coldchain::seal_block(chain_.size(), std::move(batch), prev, n_nodes_);
        chain_.push_back(blk);
        ++sealed;
    }
    return sealed;
}

Hash256 LedgerNode::canonical_state_hash() const {
    // Deterministic canonical ordering using CausalMergeEngine (topo_sort with SHA-256 tie-breaker)
    auto canonical = CausalMergeEngine::topo_sort(all_events_);
    std::ostringstream os;
    for (const auto& ev : canonical) {
        os << hash_to_hex(event_hash(ev)) << ';';
    }
    return sha256_bytes(os.str());
}

size_t LedgerNode::delivered_count() const {
    return delivered_hashes_.size();
}

bool LedgerNode::has_delivered(const Hash256& event_id) const {
    return delivered_hashes_.count(event_id) > 0;
}

const std::vector<LedgerEvent>& LedgerNode::hold_back_queue() const { return hold_back_queue_; }
const std::vector<LedgerBlock>& LedgerNode::chain() const     { return chain_; }
std::vector<LedgerBlock>&       LedgerNode::chain_mut()       { return chain_; }
size_t LedgerNode::pending_count() const                       { return pending_.size(); }
size_t LedgerNode::hold_back_count() const                     { return hold_back_queue_.size(); }
const std::vector<LedgerEvent>& LedgerNode::all_events() const { return all_events_; }
std::vector<LedgerEvent>& LedgerNode::all_events_mut()         { return all_events_; }

const std::map<uint64_t, SensorEvent>& LedgerNode::active_state_store() const {
    return active_state_store_;
}
std::map<uint64_t, SensorEvent>& LedgerNode::active_state_store_mut() {
    return active_state_store_;
}

const VectorClock& LedgerNode::local_clock() const { return local_vc_; }
MatrixClock&       LedgerNode::matrix_clock()       { return mc_; }
const MatrixClock& LedgerNode::matrix_clock() const { return mc_; }

uint8_t LedgerNode::node_id() const          { return node_id_; }
size_t  LedgerNode::events_per_block() const { return events_per_block_; }

size_t LedgerNode::active_store_bytes() const {
    return active_state_store_.size() * sizeof(SensorEvent);
}

size_t LedgerNode::total_memory_bytes() const {
    size_t events_mem = 0;
    for (const auto& ev : all_events_) {
        if (event_is_pruned(ev)) {
            events_mem += sizeof(Hash256) + sizeof(VectorClock); // Compact cryptographic commitment
        } else {
            events_mem += sizeof(SensorEvent);
        }
    }
    size_t store_mem  = active_store_bytes();
    size_t chain_mem  = chain_.size() * sizeof(LedgerBlock);
    for (const auto& blk : chain_) {
        for (const auto& ev : blk.events) {
            if (event_is_pruned(ev)) {
                chain_mem += sizeof(Hash256) + sizeof(VectorClock);
            } else {
                chain_mem += sizeof(SensorEvent);
            }
        }
    }
    size_t hashes_mem = delivered_hashes_.size() * sizeof(Hash256);
    return events_mem + store_mem + chain_mem + hashes_mem;
}

} // namespace coldchain

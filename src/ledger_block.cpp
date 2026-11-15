/// @file ledger_block.cpp
/// @brief Block construction, Merkle root tree computation, and cryptographic verification.

#include "coldchain/ledger_block.hpp"
#include "coldchain/hashing.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace coldchain {

namespace {

Hash256 combine_hashes(const Hash256& left, const Hash256& right) {
    std::string payload;
    payload.reserve(64);
    payload.append(reinterpret_cast<const char*>(left.data()), 32);
    payload.append(reinterpret_cast<const char*>(right.data()), 32);
    return sha256_bytes(payload);
}

std::vector<Hash256> build_leaf_hashes(const std::vector<LedgerEvent>& events) {
    if (events.empty()) {
        return {genesis_hash()};
    }
    std::vector<Hash256> leaves;
    leaves.reserve(events.size());
    for (const auto& ev : events) {
        leaves.push_back(event_hash(ev));
    }
    return leaves;
}

} // namespace

Hash256 compute_merkle_root(const std::vector<LedgerEvent>& events) {
    std::vector<Hash256> current = build_leaf_hashes(events);

    while (current.size() > 1) {
        std::vector<Hash256> next_level;
        next_level.reserve((current.size() + 1) / 2);

        for (size_t i = 0; i < current.size(); i += 2) {
            if (i + 1 < current.size()) {
                next_level.push_back(combine_hashes(current[i], current[i + 1]));
            } else {
                next_level.push_back(combine_hashes(current[i], current[i])); // duplicate odd leaf
            }
        }
        current = std::move(next_level);
    }

    return current.front();
}

std::vector<Hash256> generate_merkle_proof(const LedgerBlock& block, size_t event_idx) {
    std::vector<Hash256> proof;
    if (event_idx >= block.events.size()) {
        return proof;
    }

    std::vector<Hash256> current = build_leaf_hashes(block.events);
    size_t idx = event_idx;

    while (current.size() > 1) {
        size_t sibling_idx = (idx % 2 == 0) ? (idx + 1) : (idx - 1);
        if (sibling_idx >= current.size()) {
            sibling_idx = idx; // duplicate odd leaf
        }
        proof.push_back(current[sibling_idx]);

        std::vector<Hash256> next_level;
        next_level.reserve((current.size() + 1) / 2);
        for (size_t i = 0; i < current.size(); i += 2) {
            if (i + 1 < current.size()) {
                next_level.push_back(combine_hashes(current[i], current[i + 1]));
            } else {
                next_level.push_back(combine_hashes(current[i], current[i]));
            }
        }
        current = std::move(next_level);
        idx /= 2;
    }

    return proof;
}

bool verify_merkle_proof(const Hash256& merkle_root,
                         const Hash256& target_event_hash,
                         const std::vector<Hash256>& proof,
                         size_t event_idx) {
    Hash256 current = target_event_hash;
    size_t idx = event_idx;

    for (const auto& sibling : proof) {
        if (idx % 2 == 0) {
            current = combine_hashes(current, sibling);
        } else {
            current = combine_hashes(sibling, current);
        }
        idx /= 2;
    }

    return current == merkle_root;
}

// Deterministic serialization of a block's content (excluding this_hash).
static std::string serialize_block_content(const LedgerBlock& block) {
    std::ostringstream os;
    os << block.block_index << '|';

    // Append prev_hash as hex
    os << hash_to_hex(block.prev_hash) << '|';

    // Append merkle_root as hex
    os << hash_to_hex(block.merkle_root) << '|';

    // Append block_vc
    os << block.block_vc.to_string() << '|';

    // Serialize each event
    for (const auto& ev : block.events) {
        os << event_serialize(ev) << ';';
    }

    return os.str();
}

Hash256 compute_block_hash(const LedgerBlock& block) {
    return sha256_bytes(serialize_block_content(block));
}

LedgerBlock seal_block(uint64_t                 block_index,
                       std::vector<LedgerEvent> events,
                       const Hash256&           prev_hash,
                       size_t                   num_nodes) {
    LedgerBlock blk;
    blk.block_index = block_index;
    blk.events      = std::move(events);
    blk.prev_hash   = prev_hash;
    blk.block_vc    = VectorClock(num_nodes);

    // Build summary VC: element-wise max over all events
    for (const auto& ev : blk.events)
        blk.block_vc.merge(event_vc(ev));

    blk.merkle_root = compute_merkle_root(blk.events);
    blk.this_hash   = compute_block_hash(blk);
    return blk;
}

bool verify_chain(const std::vector<LedgerBlock>& chain) {
    if (chain.empty()) return true;

    // Block 0 must chain from genesis.
    Hash256 gen = genesis_hash();
    if (chain[0].prev_hash != gen) return false;

    for (size_t i = 0; i < chain.size(); ++i) {
        // Verify Merkle root commitment
        Hash256 computed_merkle = compute_merkle_root(chain[i].events);
        if (computed_merkle != chain[i].merkle_root) return false;

        // Recompute block hash and compare
        Hash256 recomputed = compute_block_hash(chain[i]);
        if (recomputed != chain[i].this_hash) return false;

        // Check linkage to previous block
        if (i > 0 && chain[i].prev_hash != chain[i - 1].this_hash)
            return false;
    }
    return true;
}

Hash256 genesis_hash() {
    Hash256 h{};
    h.fill(0);
    return h;
}

} // namespace coldchain

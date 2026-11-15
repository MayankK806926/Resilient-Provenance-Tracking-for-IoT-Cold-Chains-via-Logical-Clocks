/// @file test_pruning_safety.cpp
/// @brief PruningEngine correctness, safety oracle, excursion retention, and partition tests.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/pruning_engine.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"
#include "coldchain/events.hpp"
#include "coldchain/config.hpp"

using namespace coldchain;

static SensorEvent make_sensor(uint8_t entity, VectorClock vc,
                               float temp = 5.0f, uint32_t ship = 1) {
    SensorEvent se;
    se.shipment_id            = ship;
    se.origin_entity_id       = entity;
    se.temperature_celsius    = temp;
    se.latitude               = 0.0f;
    se.longitude              = 0.0f;
    se.local_monotonic_counter = 1;
    se.vc                     = std::move(vc);
    se.signature              = sign_event(se.serialize(), node_private_key(entity));
    se.id                     = se.compute_hash();
    return se;
}

static void gossip_sync(std::vector<LedgerNode>& nodes, size_t rounds) {
    size_t n = nodes.size();
    for (size_t r = 0; r < rounds; ++r) {
        std::vector<MatrixClock> snaps;
        for (size_t i = 0; i < n; ++i) snaps.push_back(nodes[i].matrix_clock());
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                nodes[j].matrix_clock().merge_matrix(j, i, snaps[i]);
            }
        }
    }
}

TEST_CASE("PruningEngine::is_safe_to_prune verifies dominance") {
    VectorClock event_vc(4);
    event_vc.tick(0); event_vc.tick(0);  // [2,0,0,0]

    VectorClock horizon(4);
    horizon.tick(0);  // [1,0,0,0]
    CHECK_FALSE(PruningEngine::is_safe_to_prune(event_vc, horizon));

    horizon.tick(0); horizon.tick(1); horizon.tick(2); horizon.tick(3); // [2,1,1,1]
    CHECK(PruningEngine::is_safe_to_prune(event_vc, horizon));
}

TEST_CASE("Retention policy: excursions are NEVER pruned") {
    LedgerNode node(0, 4);

    VectorClock vc(4);
    vc.tick(0);

    // Normal event (within 2-8°C)
    SensorEvent normal = make_sensor(0, vc, 5.0f);
    // Excursion event (temperature excursion 15°C!)
    vc.tick(0);
    SensorEvent excursion = make_sensor(0, vc, 15.0f);

    node.ingest_event(normal);
    node.ingest_event(excursion);

    // Artificially advance matrix clock to dominate vc
    for (size_t j = 0; j < 4; ++j) {
        for (size_t k = 0; k < 4; ++k) {
            node.matrix_clock().set(j, k, 10);
        }
    }

    CHECK(node.active_state_store().size() == 2);

    size_t violations = 0;
    std::vector<LedgerNode> oracle = {node};
    size_t pruned = PruningEngine::prune_stable_records(node, 2.0f, 8.0f, &oracle, &violations);

    CHECK(pruned == 1); // Only normal event was pruned
    CHECK(node.active_state_store().size() == 1); // Excursion remains in state store!
    CHECK(violations == 0);

    // Verify the remaining event is indeed the excursion
    const auto& remaining = node.active_state_store().begin()->second;
    CHECK(remaining.temperature_celsius == 15.0f);
}

TEST_CASE("Partition stalls pruning horizon") {
    std::vector<LedgerNode> nodes;
    for (uint8_t i = 0; i < 4; ++i) {
        nodes.emplace_back(i, 4);
    }

    // Nodes 0, 1, 2 deliver an event from node 0
    VectorClock vc0(4); vc0.tick(0);
    SensorEvent ev = make_sensor(0, vc0, 5.0f);

    nodes[0].ingest_event(ev);
    nodes[1].ingest_event(ev);
    nodes[2].ingest_event(ev);
    // Node 3 is partitioned and HAS NOT DELIVERED the event!

    // Nodes 0, 1, 2 gossip with each other (Node 3 excluded)
    for (int r = 0; r < 3; ++r) {
        MatrixClock s0 = nodes[0].matrix_clock(), s1 = nodes[1].matrix_clock(), s2 = nodes[2].matrix_clock();
        nodes[0].matrix_clock().merge_matrix(0, 1, s1); nodes[0].matrix_clock().merge_matrix(0, 2, s2);
        nodes[1].matrix_clock().merge_matrix(1, 0, s0); nodes[1].matrix_clock().merge_matrix(1, 2, s2);
        nodes[2].matrix_clock().merge_matrix(2, 0, s0); nodes[2].matrix_clock().merge_matrix(2, 1, s1);
    }

    // Node 0 checks column_min horizon:
    // Node 3's row has 0 for dimension 0, so column_min[0] MUST BE 0!
    VectorClock horizon = nodes[0].matrix_clock().column_min();
    CHECK(horizon[0] == 0);

    // Pruning must NOT remove the event from node 0!
    size_t violations = 0;
    size_t pruned = PruningEngine::prune_stable_records(nodes[0], 2.0f, 8.0f, &nodes, &violations);
    CHECK(pruned == 0);
    CHECK(nodes[0].active_state_store().size() == 1);
    CHECK(violations == 0);
}

TEST_CASE("Safety invariant: zero violations across multi-node execution") {
    std::vector<LedgerNode> nodes;
    for (uint8_t i = 0; i < 4; ++i) nodes.emplace_back(i, 4);

    VectorClock vc(4);
    for (size_t i = 0; i < 4; ++i) {
        vc.tick(i);
        SensorEvent ev = make_sensor(static_cast<uint8_t>(i), vc, 5.0f);
        for (auto& node : nodes) {
            node.ingest_event(ev);
        }
    }

    gossip_sync(nodes, 3);

    size_t total_violations = 0;
    for (auto& node : nodes) {
        PruningEngine::prune_stable_records(node, 2.0f, 8.0f, &nodes, &total_violations);
    }

    CHECK(total_violations == 0);
}

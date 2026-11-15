/// @file test_replicated_ledger.cpp
/// @brief Tests for replicated permissioned ledger, hold-back queue, and convergence under partitions.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/ledger_node.hpp"
#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

static SensorEvent make_signed_sensor(uint8_t origin, VectorClock vc, uint64_t counter, float temp = 4.0f) {
    SensorEvent se;
    se.shipment_id             = 1;
    se.origin_entity_id        = origin;
    se.temperature_celsius     = temp;
    se.latitude                = 12.0f;
    se.longitude               = 77.0f;
    se.local_monotonic_counter = counter;
    se.vc                      = std::move(vc);
    se.signature               = sign_event(se.serialize(), node_private_key(origin));
    se.id                      = se.compute_hash();
    return se;
}

TEST_CASE("Replicated ledger convergence under permuted delivery") {
    // 3 replicas
    LedgerNode node0(0, 3);
    LedgerNode node1(1, 3);
    LedgerNode node2(2, 3);

    // Create 4 causally related events
    VectorClock vc(3);
    vc.tick(0);
    auto e1 = make_signed_sensor(0, vc, 1);

    vc.tick(1);
    auto e2 = make_signed_sensor(1, vc, 2);

    vc.tick(2);
    auto e3 = make_signed_sensor(2, vc, 3);

    vc.tick(0);
    auto e4 = make_signed_sensor(0, vc, 4);

    // Deliver to replicas in completely different orders:
    // Node 0 receives in reverse order: [e4, e3, e2, e1]
    node0.ingest_event(e4);
    node0.ingest_event(e3);
    node0.ingest_event(e2);
    node0.ingest_event(e1); // Unblocks cascade through hold-back queue!

    // Node 1 receives: [e2, e1, e4, e3]
    node1.ingest_event(e2);
    node1.ingest_event(e1);
    node1.ingest_event(e4);
    node1.ingest_event(e3);

    // Node 2 receives: [e1, e2, e3, e4]
    node2.ingest_event(e1);
    node2.ingest_event(e2);
    node2.ingest_event(e3);
    node2.ingest_event(e4);

    // All replicas must end with all 4 events delivered
    CHECK(node0.all_events().size() == 4);
    CHECK(node1.all_events().size() == 4);
    CHECK(node2.all_events().size() == 4);

    // Stored ledger must be causally ordered at every replica by construction!
    CHECK(CausalMergeEngine::is_causally_valid(node0.all_events()));
    CHECK(CausalMergeEngine::is_causally_valid(node1.all_events()));
    CHECK(CausalMergeEngine::is_causally_valid(node2.all_events()));

    // Deterministic state convergence: identical canonical state hash across replicas!
    CHECK(node0.canonical_state_hash() == node1.canonical_state_hash());
    CHECK(node1.canonical_state_hash() == node2.canonical_state_hash());
}

TEST_CASE("Hold-back queue buffers future events and releases upon prerequisite delivery") {
    LedgerNode node(0, 3);

    VectorClock vc0(3); vc0.tick(0);
    auto e1 = make_signed_sensor(0, vc0, 1);

    VectorClock vc1 = vc0; vc1.tick(1);
    auto e2 = make_signed_sensor(1, vc1, 2);

    // Ingest e2 before e1
    node.ingest_event(e2);
    CHECK(node.hold_back_count() == 1);
    CHECK(node.all_events().empty());

    // Ingest e1: must deliver e1 and automatically release e2 from hold-back queue!
    node.ingest_event(e1);
    CHECK(node.hold_back_count() == 0);
    CHECK(node.all_events().size() == 2);
    CHECK(event_counter(node.all_events()[0]) == 1);
    CHECK(event_counter(node.all_events()[1]) == 2);
}

TEST_CASE("Idempotent ingest: duplicate events are safely ignored") {
    LedgerNode node(0, 3);

    VectorClock vc(3); vc.tick(0);
    auto ev = make_signed_sensor(0, vc, 1);

    CHECK(node.ingest_event(ev));
    CHECK(node.all_events().size() == 1);

    // Duplicate delivery
    CHECK(node.ingest_event(ev));
    CHECK(node.all_events().size() == 1); // Not duplicated!
    CHECK(node.delivered_count() == 1);
}

TEST_CASE("Security: forged signature and unregistered entity rejection") {
    LedgerNode node(0, 4);

    VectorClock vc(4); vc.tick(0);
    auto valid_ev = make_signed_sensor(0, vc, 1);

    // 1. Forged signature: signed with wrong secret
    auto forged_ev = valid_ev;
    forged_ev.signature = sign_event(forged_ev.serialize(), "forged_malicious_key_123");
    CHECK_FALSE(node.ingest_event(forged_ev));
    CHECK(node.all_events().empty());

    // 2. Tampered content: changed temperature after signing
    auto tampered_ev = valid_ev;
    tampered_ev.temperature_celsius = 99.9f;
    CHECK_FALSE(node.ingest_event(tampered_ev));
    CHECK(node.all_events().empty());

    // 3. Unregistered entity: node ID 10 is outside [0, 3]
    SensorEvent unreg_ev = make_signed_sensor(0, vc, 1);
    unreg_ev.origin_entity_id = 10;
    unreg_ev.signature = sign_event(unreg_ev.serialize(), node_private_key(10));
    CHECK_FALSE(node.ingest_event(unreg_ev));
    CHECK(node.all_events().empty());
}

TEST_CASE("Causal gating: custody event held back until preceding sensor readings arrive") {
    LedgerNode node(0, 3);

    VectorClock vc0(3); vc0.tick(0);
    auto s1 = make_signed_sensor(0, vc0, 1);

    vc0.tick(0);
    auto s2 = make_signed_sensor(0, vc0, 2);

    CustodyEvent ce;
    ce.shipment_id = 1;
    ce.from_entity_id = 0;
    ce.to_entity_id = 1;
    ce.custody_transfer_seq = 1;
    ce.last_sensor_seq = 2; // Gated on readings 1 and 2
    vc0.tick(0);
    ce.vc = vc0;
    ce.transfer_signature = sign_event(ce.serialize(), node_private_key(0));
    ce.id = ce.compute_hash();

    // Ingest custody event first (before s1 and s2 arrive)
    node.ingest_event(ce);
    CHECK(node.hold_back_count() == 1);
    CHECK(node.all_events().empty());

    // Ingest s1: custody still held back because s2 is missing
    node.ingest_event(s1);
    CHECK(node.all_events().size() == 1);
    CHECK(node.hold_back_count() == 1);

    // Ingest s2: unblocks custody event!
    node.ingest_event(s2);
    CHECK(node.hold_back_count() == 0);
    CHECK(node.all_events().size() == 3);

    // Stored order MUST be s1, s2, ce!
    CHECK(std::holds_alternative<SensorEvent>(node.all_events()[0]));
    CHECK(event_counter(node.all_events()[0]) == 1);
    CHECK(std::holds_alternative<SensorEvent>(node.all_events()[1]));
    CHECK(event_counter(node.all_events()[1]) == 2);
    CHECK(std::holds_alternative<CustodyEvent>(node.all_events()[2]));
}

TEST_CASE("Ablation: zeroed vector clocks fail causal readiness and stall in hold-back queue") {
    LedgerNode node(0, 3);

    VectorClock zero_vc(3); // [0, 0, 0] — ablated logical clock
    auto ev = make_signed_sensor(0, zero_vc, 1);

    // Ingest event with ablated vector clock
    node.ingest_event(ev);

    // Must be rejected by causal readiness check and stuck in hold-back queue!
    CHECK(node.hold_back_count() == 1);
    CHECK(node.all_events().empty());
}

TEST_CASE("Cross-entity concurrency diamond: dock inspection and carrier transit merge at custody receive") {
    LedgerNode node(0, 3);

    // Carrier (origin 0) transit readings
    VectorClock carrier_vc(3);
    carrier_vc.tick(0);
    auto s_carrier = make_signed_sensor(0, carrier_vc, 1);

    // Carrier departure event
    carrier_vc.tick(0);
    CustodyEvent ce_depart;
    ce_depart.shipment_id          = 1;
    ce_depart.from_entity_id       = 0;
    ce_depart.to_entity_id         = 1;
    ce_depart.origin_entity_id     = 0;
    ce_depart.custody_transfer_seq = 1;
    ce_depart.vc                   = carrier_vc;
    ce_depart.transfer_signature   = sign_event(ce_depart.serialize(), node_private_key(0));
    ce_depart.id                   = ce_depart.compute_hash();

    // Receiver (origin 1) dock inspection reading (concurrent with carrier transit!)
    VectorClock dock_vc(3);
    dock_vc.tick(1);
    auto s_dock = make_signed_sensor(1, dock_vc, 1);

    // Receiver arrival event (merges carrier_vc and dock_vc, then ticks 1)
    VectorClock recv_vc = carrier_vc;
    recv_vc.merge(dock_vc);
    recv_vc.tick(1);

    CustodyEvent ce_recv;
    ce_recv.shipment_id          = 1;
    ce_recv.from_entity_id       = 0;
    ce_recv.to_entity_id         = 1;
    ce_recv.origin_entity_id     = 1;
    ce_recv.custody_transfer_seq = 2;
    ce_recv.vc                   = recv_vc;
    ce_recv.transfer_signature   = sign_event(ce_recv.serialize(), node_private_key(1));
    ce_recv.id                   = ce_recv.compute_hash();

    // Ingest ce_recv FIRST: must be held back because predecessors haven't arrived!
    node.ingest_event(ce_recv);
    CHECK(node.hold_back_count() == 1);
    CHECK(node.all_events().empty());

    // Ingest s_dock: delivers s_dock, but ce_recv remains held back because carrier events missing!
    node.ingest_event(s_dock);
    CHECK(node.all_events().size() == 1);
    CHECK(node.hold_back_count() == 1);

    // Ingest s_carrier: delivers s_carrier, but ce_recv still missing ce_depart!
    node.ingest_event(s_carrier);
    CHECK(node.all_events().size() == 2);
    CHECK(node.hold_back_count() == 1);

    // Ingest ce_depart: unblocks ce_recv!
    node.ingest_event(ce_depart);
    CHECK(node.hold_back_count() == 0);
    CHECK(node.all_events().size() == 4);

    // Final delivered event must be ce_recv!
    CHECK(node.all_events().back().index() == 1); // CustodyEvent
    CHECK(std::get<CustodyEvent>(node.all_events().back()).origin_entity_id == 1);
    CHECK(CausalMergeEngine::is_causally_valid(node.all_events()));
}


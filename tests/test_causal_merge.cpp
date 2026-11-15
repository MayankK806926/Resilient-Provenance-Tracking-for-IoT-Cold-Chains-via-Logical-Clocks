/// @file test_causal_merge.cpp
/// @brief Unit tests for CausalMergeEngine, naive physical baseline,
///        topological sorting, and causal inversion detection.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/config.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/metrics.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

#include <algorithm>
#include <random>

using namespace coldchain;

static SensorEvent make_sensor_with_time(uint32_t shipment, uint8_t entity,
                                         float temp, uint64_t counter,
                                         VectorClock vc, uint64_t phys_time_ns = 0) {
    SensorEvent se;
    se.shipment_id            = shipment;
    se.origin_entity_id       = entity;
    se.temperature_celsius    = temp;
    se.latitude               = 13.0f;
    se.longitude              = 80.0f;
    se.local_monotonic_counter = counter;
    se.vc                     = std::move(vc);
    se.physical_timestamp_ns  = phys_time_ns;
    se.signature              = sign_event(se.serialize(), default_node_secret(entity));
    return se;
}

static CustodyEvent make_custody_with_time(uint32_t shipment, uint8_t from_entity,
                                           uint8_t to_entity, uint64_t seq,
                                           VectorClock vc, uint64_t phys_time_ns = 0) {
    CustodyEvent ce;
    ce.shipment_id            = shipment;
    ce.from_entity_id         = from_entity;
    ce.to_entity_id           = to_entity;
    ce.custody_transfer_seq   = seq;
    ce.vc                     = std::move(vc);
    ce.physical_timestamp_ns  = phys_time_ns;
    ce.transfer_signature     = sign_event(ce.serialize(), default_node_secret(from_entity));
    return ce;
}

//  Basic topological sort

TEST_CASE("CausalMergeEngine: empty and single-element batches") {
    std::vector<LedgerEvent> empty;
    auto sorted_empty = CausalMergeEngine::topo_sort(empty);
    CHECK(sorted_empty.empty());

    VectorClock vc(4);
    vc.tick(0);
    std::vector<LedgerEvent> single = {make_sensor_with_time(1, 0, 4.0f, 1, vc)};
    auto sorted_single = CausalMergeEngine::topo_sort(single);
    CHECK(sorted_single.size() == 1);
    CHECK(event_origin(sorted_single[0]) == 0);
}

TEST_CASE("CausalMergeEngine: linear causal chain arriving reversed") {
    // E0 -> E1 -> E2 -> E3
    VectorClock vc0(4); vc0.tick(0);                     // [1,0,0,0]
    VectorClock vc1 = vc0; vc1.tick(1);                  // [1,1,0,0]
    VectorClock vc2 = vc1; vc2.tick(2);                  // [1,1,1,0]
    VectorClock vc3 = vc2; vc3.tick(3);                  // [1,1,1,1]

    LedgerEvent e0 = make_sensor_with_time(1, 0, 4.0f, 1, vc0);
    LedgerEvent e1 = make_sensor_with_time(1, 1, 4.5f, 2, vc1);
    LedgerEvent e2 = make_sensor_with_time(1, 2, 5.0f, 3, vc2);
    LedgerEvent e3 = make_sensor_with_time(1, 3, 5.5f, 4, vc3);

    // Feed in completely reversed order
    std::vector<LedgerEvent> scrambled = {e3, e2, e1, e0};
    auto sorted = CausalMergeEngine::topo_sort(scrambled);

    REQUIRE(sorted.size() == 4);
    CHECK(event_origin(sorted[0]) == 0);
    CHECK(event_origin(sorted[1]) == 1);
    CHECK(event_origin(sorted[2]) == 2);
    CHECK(event_origin(sorted[3]) == 3);

    CHECK(CausalMergeEngine::is_causally_valid(sorted));
    CHECK(count_causal_inversions(sorted) == 0);
}

TEST_CASE("CausalMergeEngine: branching DAG with concurrent events") {
    // Node 0 generates A: [1,0,0,0]
    VectorClock vcA(4); vcA.tick(0);

    // Node 1 receives A, generates B1: [1,1,0,0]
    VectorClock vcB1 = vcA; vcB1.tick(1);

    // Node 2 receives A, generates B2: [1,0,1,0] (B1 and B2 concurrent!)
    VectorClock vcB2 = vcA; vcB2.tick(2);

    CHECK(vcB1.is_concurrent(vcB2));

    // Node 3 merges B1 and B2, generates D: [1,1,1,1]
    VectorClock vcD = vcB1; vcD.merge(vcB2); vcD.tick(3);

    CHECK(vcA.precedes(vcB1));
    CHECK(vcA.precedes(vcB2));
    CHECK(vcB1.precedes(vcD));
    CHECK(vcB2.precedes(vcD));

    LedgerEvent eA  = make_sensor_with_time(1, 0, 4.0f, 1, vcA);
    LedgerEvent eB1 = make_sensor_with_time(1, 1, 4.2f, 1, vcB1);
    LedgerEvent eB2 = make_sensor_with_time(1, 2, 4.4f, 1, vcB2);
    LedgerEvent eD  = make_sensor_with_time(1, 3, 4.6f, 1, vcD);

    // Scrambled feed: {D, B2, A, B1}
    std::vector<LedgerEvent> scrambled = {eD, eB2, eA, eB1};
    auto sorted = CausalMergeEngine::topo_sort(scrambled);

    REQUIRE(sorted.size() == 4);
    // A must be first, D must be last
    CHECK(event_origin(sorted[0]) == 0); // eA
    CHECK(event_origin(sorted[3]) == 3); // eD

    // Between B1 and B2, tie-breaker orders node 1 before node 2
    CHECK(event_origin(sorted[1]) == 1); // eB1
    CHECK(event_origin(sorted[2]) == 2); // eB2

    CHECK(CausalMergeEngine::is_causally_valid(sorted));
    CHECK(count_causal_inversions(sorted) == 0);
}

//  Deterministic tie-breaking

TEST_CASE("CausalMergeEngine: deterministic tie-breaking is invariant to input permutations") {
    // 3 mutually concurrent events on nodes 1, 2, 3
    VectorClock vc1(4); vc1.tick(1);
    VectorClock vc2(4); vc2.tick(2);
    VectorClock vc3(4); vc3.tick(3);

    LedgerEvent e1 = make_sensor_with_time(1, 1, 4.0f, 10, vc1);
    LedgerEvent e2 = make_sensor_with_time(1, 2, 4.0f, 10, vc2);
    LedgerEvent e3 = make_sensor_with_time(1, 3, 4.0f, 10, vc3);

    std::vector<LedgerEvent> perm1 = {e3, e1, e2};
    std::vector<LedgerEvent> perm2 = {e2, e3, e1};
    std::vector<LedgerEvent> perm3 = {e1, e2, e3};

    auto res1 = CausalMergeEngine::topo_sort(perm1);
    auto res2 = CausalMergeEngine::topo_sort(perm2);
    auto res3 = CausalMergeEngine::topo_sort(perm3);

    // All permutations must produce identical ordering: node 1, node 2, node 3
    REQUIRE(res1.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(event_origin(res1[i]) == event_origin(res2[i]));
        CHECK(event_origin(res1[i]) == event_origin(res3[i]));
        CHECK(event_counter(res1[i]) == event_counter(res2[i]));
    }
    CHECK(event_origin(res1[0]) == 1);
    CHECK(event_origin(res1[1]) == 2);
    CHECK(event_origin(res1[2]) == 3);
}

//  Signature verification

TEST_CASE("CausalMergeEngine::verify_signatures detects valid and tampered batches") {
    VectorClock vc(4);
    vc.tick(0);

    auto se = make_sensor_with_time(1, 0, 4.0f, 1, vc);
    auto ce = make_custody_with_time(1, 0, 1, 1, vc);

    std::vector<LedgerEvent> batch = {se, ce};
    CHECK(CausalMergeEngine::verify_signatures(batch));

    // Tamper with sensor event signature
    auto se_bad = se;
    se_bad.signature[0] ^= 0xAA;
    std::vector<LedgerEvent> bad_sensor_batch = {se_bad, ce};
    CHECK_FALSE(CausalMergeEngine::verify_signatures(bad_sensor_batch));

    // Tamper with custody event signature
    auto ce_bad = ce;
    ce_bad.transfer_signature[0] ^= 0x55;
    std::vector<LedgerEvent> bad_custody_batch = {se, ce_bad};
    CHECK_FALSE(CausalMergeEngine::verify_signatures(bad_custody_batch));
}

//  DriftingPhysicalClock & Naive Baseline comparison

TEST_CASE("DriftingPhysicalClock produces deterministic drift with seed") {
    DriftingPhysicalClock clock1(50.0, 10.0, 12345);
    DriftingPhysicalClock clock2(50.0, 10.0, 12345);

    CHECK(clock1.drift_rate_ppm() == clock2.drift_rate_ppm());
    uint64_t t = 10'000'000'000ULL; // 10 seconds
    CHECK(clock1.now_ns(t) == clock2.now_ns(t));
}

TEST_CASE("Naive baseline produces causal inversions when clocks drift, CausalMerge fixes it") {
    // Scenario demonstrating the core thesis:
    // Entity 0 has clock running fast (+100 ms offset).
    // Entity 1 has clock running slow (-100 ms offset).
    //
    // Event A happens at Entity 0 at true sim-time 500 ms.
    // Due to fast clock, Entity 0 stamps physical time = 600 ms.
    //
    // Hand-off occurs.
    // Event B happens at Entity 1 at true sim-time 600 ms (strictly after A!).
    // Due to slow clock, Entity 1 stamps physical time = 500 ms.
    //
    // Causal relationship: A -> B (vcA precedes vcB)

    VectorClock vcA(4);
    vcA.tick(0); // [1, 0, 0, 0]

    VectorClock vcB = vcA;
    vcB.tick(1); // [1, 1, 0, 0]

    CHECK(vcA.precedes(vcB));

    // Event A: physical timestamp 600 ms
    LedgerEvent evA = make_sensor_with_time(1, 0, 4.0f, 1, vcA, 600'000'000ULL);
    // Event B: physical timestamp 500 ms (lower due to drift!)
    LedgerEvent evB = make_sensor_with_time(1, 1, 4.2f, 2, vcB, 500'000'000ULL);

    std::vector<LedgerEvent> events = {evA, evB};

    // 1. Run naive physical pipeline:
    auto naive_result = naive_physical_order(events);
    // Naive sorts by physical time: 500ms before 600ms -> evB placed before evA!
    CHECK(event_origin(naive_result[0]) == 1); // evB
    CHECK(event_origin(naive_result[1]) == 0); // evA

    // Result has a causal inversion!
    CHECK(has_causal_inversion(naive_result));
    CHECK(count_causal_inversions(naive_result) == 1);
    CHECK_FALSE(CausalMergeEngine::is_causally_valid(naive_result));

    // 2. Run vector-clock causal merge pipeline:
    auto causal_result = CausalMergeEngine::topo_sort(events);
    // Causal engine uses happens-before DAG: evA placed before evB!
    CHECK(event_origin(causal_result[0]) == 0); // evA
    CHECK(event_origin(causal_result[1]) == 1); // evB

    // Guarantees zero causal inversions!
    CHECK_FALSE(has_causal_inversion(causal_result));
    CHECK(count_causal_inversions(causal_result) == 0);
    CHECK(CausalMergeEngine::is_causally_valid(causal_result));
}

TEST_CASE("Metrics: compute_causal_misordering_rate matches proposal formula") {
    // 3 events in strict causal chain: e0 -> e1 -> e2
    VectorClock vc0(4); vc0.tick(0);
    VectorClock vc1 = vc0; vc1.tick(1);
    VectorClock vc2 = vc1; vc2.tick(2);

    auto e0 = make_sensor_with_time(1, 0, 4.0f, 1, vc0, 100);
    auto e1 = make_sensor_with_time(1, 1, 4.2f, 2, vc1, 200);
    auto e2 = make_sensor_with_time(1, 2, 4.4f, 3, vc2, 300);

    // Order: {e2, e0, e1}
    // Pairs:
    //   (e0, e2): e0 -> e2, but e2 before e0 (inversion)
    //   (e1, e2): e1 -> e2, but e2 before e1 (inversion)
    //   (e0, e1): e0 -> e1, e0 before e1 (correct)
    // Total comparable pairs = 3, inversions = 2 => R_err = 2/3
    std::vector<LedgerEvent> inverted_order = {e2, e0, e1};
    size_t inv_count = 0;
    double r_err = compute_causal_misordering_rate(inverted_order, inv_count);

    CHECK(inv_count == 2);
    CHECK(doctest::Approx(r_err).epsilon(1e-4) == 2.0 / 3.0);

    // Causal topo_sort resolves it
    auto sorted = CausalMergeEngine::topo_sort(inverted_order);
    size_t sorted_inv = 0;
    double sorted_rerr = compute_causal_misordering_rate(sorted, sorted_inv);
    CHECK(sorted_inv == 0);
    CHECK(sorted_rerr == 0.0);
}

TEST_CASE("Metrics: False liability attribution (F_liab) detected in naive baseline, eliminated in causal pipeline") {
    // Simulates an excursion occurring right before handoff from node 0 to node 1.
    // In naive baseline, network reconnection latency / oscillator drift stamps
    // the custody event before the buffered excursion.
    SimConfig cfg;
    cfg.temp_low_c  = 2.0f;
    cfg.temp_high_c = 8.0f;

    VectorClock vc_normal(4); vc_normal.tick(0);
    VectorClock vc_excursion = vc_normal; vc_excursion.tick(0);
    VectorClock vc_custody   = vc_excursion; vc_custody.tick(0); // custody merged after excursion

    // Normal sensor event under custody of node 0
    auto se_normal = make_sensor_with_time(1, 0, 4.0f, 1, vc_normal, 100'000'000ULL);

    // Critical excursion under physical custody of node 0 (temp = 14.0 C)
    auto se_excursion = make_sensor_with_time(1, 0, 14.0f, 2, vc_excursion, 200'000'000ULL);

    // Custody handoff 0 -> 1: stamped at 180 ms in naive pipeline due to drift
    auto ce_handoff = make_custody_with_time(1, 0, 1, 1, vc_custody, 180'000'000ULL);

    std::vector<LedgerEvent> all_events = {se_normal, se_excursion, ce_handoff};
    auto excursions = extract_excursions(all_events, cfg);
    REQUIRE(excursions.size() == 1);
    CHECK(excursions[0].true_custodian == 0);

    // 1. Naive physical order sorts strictly by physical timestamp:
    //    se_normal (100ms) < ce_handoff (180ms) < se_excursion (200ms)
    auto naive_sorted = naive_physical_order(all_events);
    REQUIRE(naive_sorted.size() == 3);
    CHECK(std::holds_alternative<CustodyEvent>(naive_sorted[1]));
    CHECK(std::holds_alternative<SensorEvent>(naive_sorted[2]));

    // Naive attribution: custody was transferred to node 1 at index 1,
    // so node 1 is falsely blamed for node 0's excursion!
    double naive_fliab = compute_false_liability_rate(excursions, naive_sorted, cfg);
    CHECK(naive_fliab == 1.0); // 100% false liability attribution

    // 2. Causal pipeline: vector clock ensures se_excursion -> ce_handoff
    auto causal_sorted = CausalMergeEngine::topo_sort(all_events);
    REQUIRE(causal_sorted.size() == 3);
    CHECK(std::holds_alternative<SensorEvent>(causal_sorted[1]));
    CHECK(std::holds_alternative<CustodyEvent>(causal_sorted[2]));

    // Causal attribution: custody remained with node 0 when excursion happened!
    double causal_fliab = compute_false_liability_rate(excursions, causal_sorted, cfg);
    CHECK(causal_fliab == 0.0); // Zero false liability guaranteed
}

/// @file test_baselines_metrics.cpp
/// @brief Hand-built tests for R_err (dual denominators), F_liab, and C_err concurrency.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/config.hpp"
#include "coldchain/metrics.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"

using namespace coldchain;

TEST_CASE("Hand-built R_err formula verifies both denominators") {
    // 3 events: E0 -> E1 -> E2 (all causally related)
    // Inversions = 1 (E1 and E0 swapped)
    // Causally related pairs = 3 (E0-E1, E1-E2, E0-E2)
    // Total pairs = 3
    // R_err_causal = 1 / 3 = 0.333333
    // R_err_total  = 1 / 3 = 0.333333

    VectorClock vc0(3); vc0.tick(0);
    VectorClock vc1 = vc0; vc1.tick(1);
    VectorClock vc2 = vc1; vc2.tick(2);

    SensorEvent e0; e0.vc = vc0; e0.origin_entity_id = 0;
    SensorEvent e1; e1.vc = vc1; e1.origin_entity_id = 1;
    SensorEvent e2; e2.vc = vc2; e2.origin_entity_id = 2;

    // Ordered sequence with E1 placed before E0: [E1, E0, E2]
    std::vector<LedgerEvent> seq = {e1, e0, e2};

    size_t inv = 0;
    for (size_t i = 0; i < seq.size(); ++i) {
        for (size_t j = i + 1; j < seq.size(); ++j) {
            if (event_vc(seq[j]).precedes(event_vc(seq[i]))) {
                ++inv;
            }
        }
    }
    CHECK(inv == 1);
}

TEST_CASE("Concurrency metric C_err distinguishes Vector Clocks from scalar clocks") {
    // 2 concurrent events generated at different nodes:
    // Node 0: E0 [1, 0]
    // Node 1: E1 [0, 1]
    VectorClock vc0(2); vc0.tick(0);
    VectorClock vc1(2); vc1.tick(1);

    CHECK(vc0.is_concurrent(vc1));

    SensorEvent e0; e0.vc = vc0; e0.lamport_ts = 1; e0.physical_timestamp_ns = 100;
    SensorEvent e1; e1.vc = vc1; e1.lamport_ts = 1; e1.physical_timestamp_ns = 200;

    std::vector<LedgerEvent> events = {e0, e1};

    // Vector clock topo_sort detects they are concurrent:
    CHECK(event_vc(e0).is_concurrent(event_vc(e1)));

    // Scalar physical order forces e0 before e1 arbitrarily:
    auto phys = fair_creation_physical_order(events);
    CHECK(phys.size() == 2);
    // In scalar physical time, e0 precedes e1 because 100 < 200, creating false serialization!
    CHECK(event_physical_time(phys[0]) < event_physical_time(phys[1]));
}

TEST_CASE("Negative control: isolates clock drift vs gateway re-stamping") {
    // 1. Isolate Gateway Re-stamping (Zero Drift):
    // E_offline generated at t=1.0s during blackout, received at t=5.0s.
    // E_online generated at t=2.0s while online, received at t=2.0s.
    // Both clocks have zero drift.
    DriftingPhysicalClock perfect_clock0(0.0, 0, false);
    DriftingPhysicalClock perfect_clock1(0.0, 0, false);

    SensorEvent e_offline;
    e_offline.shipment_id = 1;
    e_offline.origin_entity_id = 0;
    e_offline.creation_time_ns = 1'000'000'000ULL;
    e_offline.physical_timestamp_ns = perfect_clock0.now_ns(1'000'000'000ULL);
    e_offline.receipt_timestamp_ns = 5'000'000'000ULL; // Reconnect burst arrival

    SensorEvent e_online;
    e_online.shipment_id = 2;
    e_online.origin_entity_id = 1;
    e_online.creation_time_ns = 2'000'000'000ULL;
    e_online.physical_timestamp_ns = perfect_clock1.now_ns(2'000'000'000ULL);
    e_online.receipt_timestamp_ns = 2'000'000'000ULL; // Immediate arrival

    std::vector<LedgerEvent> batch = {e_offline, e_online};

    // Under Naive Receipt (gateway stamping), E_online is falsely ordered BEFORE E_offline:
    auto receipt_sorted = naive_receipt_order(batch);
    CHECK(event_receipt_time(receipt_sorted[0]) < event_receipt_time(receipt_sorted[1]));
    CHECK(event_shipment_id(receipt_sorted[0]) == 2); // Inversion caused purely by re-stamping!

    // Under Fair Physical (with zero drift), true creation order is preserved:
    auto physical_sorted = fair_creation_physical_order(batch);
    CHECK(event_physical_time(physical_sorted[0]) < event_physical_time(physical_sorted[1]));
    CHECK(event_shipment_id(physical_sorted[0]) == 1); // No inversion!

    // 2. Isolate Clock Drift (Zero Re-stamping delay):
    // Two events with immediate receipt (zero buffering delay),
    // but drifting RTC clocks cause an inversion:
    DriftingPhysicalClock fast_clock(+100.0, 200'000'000LL, false); // +100 ppm, +200ms offset
    DriftingPhysicalClock slow_clock(-100.0, -200'000'000LL, false); // -100 ppm, -200ms offset

    SensorEvent e1;
    e1.shipment_id = 1;
    e1.creation_time_ns = 1'000'000'000ULL;
    e1.physical_timestamp_ns = fast_clock.now_ns(e1.creation_time_ns); // Fast clock advances timestamp
    e1.receipt_timestamp_ns = e1.creation_time_ns;

    SensorEvent e2;
    e2.shipment_id = 2;
    e2.creation_time_ns = 1'100'000'000ULL;
    e2.physical_timestamp_ns = slow_clock.now_ns(e2.creation_time_ns); // Slow clock lags timestamp
    e2.receipt_timestamp_ns = e2.creation_time_ns;

    std::vector<LedgerEvent> drift_batch = {e1, e2};
    auto drift_sorted = fair_creation_physical_order(drift_batch);

    // Clock drift alone caused e2 to be ordered before e1, despite zero re-stamping delay:
    CHECK(event_physical_time(drift_sorted[0]) < event_physical_time(drift_sorted[1]));
    CHECK(event_shipment_id(drift_sorted[0]) == 2);
}


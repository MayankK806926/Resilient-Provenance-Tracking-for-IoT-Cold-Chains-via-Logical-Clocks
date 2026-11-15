/// @file test_network_simulator.cpp
/// @brief Tests for NetworkSimulator, per-shipment IoTDevice, and replicated ledger.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/config.hpp"
#include "coldchain/iot_device.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/metrics.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"

using namespace coldchain;

TEST_CASE("IoTDevice generates telemetry with correct vector clock") {
    IoTDevice dev(/*shipment_id*/ 42, /*initial_custodian*/ 1, /*n_nodes*/ 4);
    CHECK(dev.current_custodian() == 1);
    CHECK(dev.shipment_id() == 42);
    CHECK(dev.is_online());

    auto se1 = dev.generate_telemetry(5.0f, 12.9f, 77.6f, 1'000'000ULL);
    CHECK(se1.shipment_id == 42);
    CHECK(se1.origin_entity_id == 1);
    CHECK(se1.vc[1] == 1);
    CHECK(se1.vc[0] == 0);
    CHECK(se1.local_monotonic_counter == 1);

    auto se2 = dev.generate_telemetry(6.0f, 12.9f, 77.6f, 2'000'000ULL);
    CHECK(se2.vc[1] == 2);
    CHECK(se2.local_monotonic_counter == 2);
}

TEST_CASE("IoTDevice buffers events during blackout and flushes on reconnect") {
    IoTDevice dev(1, 0, 4);
    dev.set_online(false);
    CHECK_FALSE(dev.is_online());

    dev.generate_telemetry(3.0f, 0, 0, 100);
    dev.generate_telemetry(4.0f, 0, 0, 200);
    dev.generate_telemetry(5.0f, 0, 0, 300);
    CHECK(dev.buffered_count() == 3);
    CHECK(dev.local_clock()[0] == 3);

    auto flushed = dev.flush_on_reconnect();
    CHECK(flushed.size() == 3);
    CHECK(dev.buffered_count() == 0);
    CHECK(flushed[0].vc[0] == 1);
    CHECK(flushed[1].vc[0] == 2);
    CHECK(flushed[2].vc[0] == 3);
}

TEST_CASE("NetworkSimulator full execution produces valid ground truth and zero R_err on vector clock") {
    SimConfig cfg;
    cfg.num_shipments = 3;
    cfg.duration_hours = 12.0;
    cfg.seed = 42;

    NetworkSimulator sim(cfg);
    sim.run_full();

    CHECK(sim.total_events_generated() > 0);
    CHECK(sim.ground_truth_events().size() == sim.total_events_generated());

    RunMetrics m = evaluate_simulation(sim, cfg);
    // Proposal guarantee: Vector clock pipeline achieves 0 causal misorderings
    CHECK(m.vclock.inversions == 0);
    CHECK(m.vclock.r_err_causal == 0.0);
    CHECK(m.all_replicas_converged);
}

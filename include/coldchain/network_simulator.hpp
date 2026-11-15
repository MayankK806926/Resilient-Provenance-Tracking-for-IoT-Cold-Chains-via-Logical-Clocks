#pragma once
/// @file network_simulator.hpp
/// @brief Discrete-event network simulator driving dual-pipeline comparison.

#include <cstdint>
#include <functional>
#include <queue>
#include <random>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/iot_device.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

struct ScheduledMessage {
    double   fire_time_ms;
    uint8_t  from;
    uint8_t  to;
    LedgerEvent payload;

    bool operator>(const ScheduledMessage& o) const {
        return fire_time_ms > o.fire_time_ms;
    }
};

struct Shipment {
    uint32_t shipment_id;
    std::vector<uint8_t> route;
    size_t current_leg = 0;
    double next_sample_ms = 0.0;
    double leg_end_ms = 0.0;
    bool   finished = false;
};

struct BlackoutEvent {
    double start_ms;
    double end_ms;
    uint8_t node_id;
};

class NetworkSimulator {
public:
    explicit NetworkSimulator(const SimConfig& cfg);

    void add_shipment(uint32_t shipment_id, std::vector<uint8_t> route);
    void inject_blackout(uint8_t node_id, double duration_ms);

    void step(double delta_time_ms);
    void run_until(double end_time_ms);
    void run_full();
    void gossip_rounds(size_t rounds);

    LedgerNode& vclock_node(size_t idx);
    const LedgerNode& vclock_node(size_t idx) const;

    LedgerNode& naive_node(size_t idx);
    const LedgerNode& naive_node(size_t idx) const;

    size_t num_nodes() const;
    double sim_clock_ms() const;

    const std::vector<LedgerEvent>& ground_truth_events() const;
    const std::vector<LedgerEvent>& vclock_received_events() const;
    const std::vector<LedgerEvent>& naive_received_events() const;

    size_t total_events_generated() const;
    size_t total_blackouts_injected() const;

private:
    void generate_random_blackouts();
    void process_telemetry(Shipment& ship);
    void process_custody_transfer(Shipment& ship);
    void deliver_pending_messages();
    double sample_tx_jitter();
    double sample_blackout_duration();

    SimConfig cfg_;
    std::mt19937_64 rng_;
    double sim_clock_ms_ = 0.0;

    std::vector<IoTDevice> devices_;
    std::vector<DriftingPhysicalClock> phys_clocks_;
    std::vector<LedgerNode> vclock_nodes_;
    std::vector<LedgerNode> naive_nodes_;

    std::vector<Shipment> shipments_;

    using MsgQueue = std::priority_queue<ScheduledMessage,
                                         std::vector<ScheduledMessage>,
                                         std::greater<ScheduledMessage>>;
    MsgQueue vclock_queue_;
    MsgQueue naive_queue_;

    std::vector<BlackoutEvent> blackouts_;

    std::vector<LedgerEvent> ground_truth_;
    std::vector<LedgerEvent> vclock_received_;
    std::vector<LedgerEvent> naive_received_;

    size_t total_blackouts_ = 0;
    uint64_t custody_seq_ = 0;
};

} // namespace coldchain

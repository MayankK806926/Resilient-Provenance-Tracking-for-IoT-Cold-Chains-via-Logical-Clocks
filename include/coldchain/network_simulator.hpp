#pragma once
/// @file network_simulator.hpp
/// @brief Discrete-event cold-chain network simulator with independent ground truth,
///        per-shipment loggers, concurrent lifecycles, and baseline comparisons.

#include <cstdint>
#include <map>
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
    double      fire_time_ms;
    uint8_t     from_node;
    uint8_t     to_node;
    LedgerEvent payload;
    bool        is_gossip = false;

    bool operator>(const ScheduledMessage& o) const {
        return fire_time_ms > o.fire_time_ms;
    }
};

struct ShipmentLeg {
    uint8_t custodian;
    double  start_ms;
    double  end_ms;
};

struct Shipment {
    uint32_t                 shipment_id;
    std::vector<uint8_t>     route;
    size_t                   current_leg_idx = 0;
    std::vector<ShipmentLeg> legs;
    double                   start_time_ms = 0.0;
    double                   next_sample_ms = 0.0;
    bool                     finished = false;
    uint64_t                 custody_transfers_count = 0;
};

struct CustodyInterval {
    uint32_t shipment_id;
    uint8_t  custodian_id;
    double   start_ms;
    double   end_ms;
};

struct BlackoutPeriod {
    double  start_ms;
    double  end_ms;
    uint8_t entity_id;
};

struct NetworkPartition {
    double  start_ms;
    double  end_ms;
    uint8_t node_a;
    uint8_t node_b;
};

struct ExcursionEpisode {
    uint32_t shipment_id;
    double   start_ms;
    double   end_ms;
    float    peak_temp_c;
};

class NetworkSimulator {
public:
    explicit NetworkSimulator(const SimConfig& cfg);

    void add_shipment(uint32_t shipment_id, std::vector<uint8_t> route, double start_offset_hours = 0.0);
    void inject_blackout(uint8_t node_id, double duration_ms);
    void inject_partition(uint8_t node_a, uint8_t node_b, double duration_ms);

    void step(double delta_time_ms);
    void run_until(double end_time_ms);
    void run_full();

    // Synchronous matrix clock gossip with network partition enforcement
    void gossip_rounds(size_t rounds);

    // Node accessors
    LedgerNode& vclock_node(size_t idx);
    const LedgerNode& vclock_node(size_t idx) const;
    const std::vector<LedgerNode>& all_vclock_nodes() const;

    size_t num_nodes() const;
    double sim_clock_ms() const;

    // Ground truth and received logs
    const std::vector<LedgerEvent>& ground_truth_events() const;
    const std::vector<CustodyInterval>& true_custody_intervals() const;

    // Independent ground-truth causality queries (completely separate from VCs)
    bool is_ground_truth_preceding(size_t event_idx_a, size_t event_idx_b) const;

    // Statistics & Counters
    size_t total_events_generated() const;
    size_t total_blackouts_injected() const;
    size_t total_partitions_injected() const;
    size_t gossip_messages_sent() const;
    size_t gossip_bytes_sent() const;
    size_t safety_violations_detected() const;

private:
    void setup_shipments();
    void generate_failure_events();
    void process_telemetry(Shipment& ship);
    void process_custody_transfer(Shipment& ship);
    void broadcast_event(const LedgerEvent& ev, uint8_t origin_node);
    void deliver_pending_messages();
    bool is_partitioned(uint8_t node_a, uint8_t node_b, double time_ms) const;
    bool is_node_in_blackout(uint8_t node_id, double time_ms) const;
    float sample_ambient_temperature(uint32_t shipment_id, double time_ms);
    double sample_tx_jitter();
    double sample_handoff_delay();

    SimConfig cfg_;
    std::mt19937_64 rng_;
    double sim_clock_ms_ = 0.0;
    double last_gossip_time_ms_ = 0.0;

    std::vector<Shipment> shipments_;
    std::vector<IoTDevice> shipment_loggers_;
    std::vector<DriftingPhysicalClock> shipment_clocks_;
    std::vector<uint64_t> entity_lamport_clocks_;
    std::map<uint32_t, std::vector<VectorClock>> entity_shipment_vcs_;
    std::map<uint32_t, std::vector<uint64_t>> entity_shipment_counters_;

    std::vector<LedgerNode> vclock_nodes_;

    using MsgQueue = std::priority_queue<ScheduledMessage,
                                         std::vector<ScheduledMessage>,
                                         std::greater<ScheduledMessage>>;
    MsgQueue message_queue_;

    std::vector<BlackoutPeriod>   blackouts_;
    std::vector<NetworkPartition> partitions_;
    std::vector<ExcursionEpisode> excursion_episodes_;
    std::vector<CustodyInterval>  custody_intervals_;

    std::vector<LedgerEvent> ground_truth_;

    // Independent ground truth causality: event_id -> index, and explicit edges
    std::map<Hash256, size_t> event_index_map_;
    std::vector<std::vector<bool>> gt_causality_matrix_;

    size_t total_blackouts_ = 0;
    size_t total_partitions_ = 0;
    size_t gossip_messages_sent_ = 0;
    size_t gossip_bytes_sent_ = 0;
    size_t safety_violations_ = 0;
    uint64_t custody_seq_ = 0;
};

} // namespace coldchain

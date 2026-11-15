#pragma once
/// @file iot_device.hpp
/// @brief Edge IoT cold-chain sensor logger with offline buffering and causal tracking.

#include <cstdint>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class IoTDevice {
public:
    IoTDevice(uint32_t shipment_id, uint8_t initial_custodian, size_t n_nodes);

    void set_online(bool online);
    bool is_online() const;

    void set_current_custodian(uint8_t custodian_id);
    uint8_t current_custodian() const;

    // Generate reading with true time, drifted physical RTC time, and Lamport ts
    SensorEvent generate_telemetry(float temperature, float lat, float lon,
                                   uint64_t true_sim_time_ns,
                                   const DriftingPhysicalClock* phys_clock = nullptr);

    std::vector<SensorEvent> flush_on_reconnect();
    size_t buffered_count() const;

    const VectorClock& local_clock() const;
    VectorClock& local_clock_mut();
    void set_local_clock(const VectorClock& vc);

    uint64_t current_lamport_ts() const;
    void merge_lamport_ts(uint64_t incoming_ts);

    uint64_t current_counter() const;
    uint64_t current_custody_seq() const;
    void set_current_custody_seq(uint64_t seq);

    uint32_t shipment_id() const;

private:
    uint32_t     shipment_id_;
    uint8_t      current_custodian_;
    size_t       n_nodes_;
    VectorClock  local_vc_;
    LamportClock lamport_clock_;
    std::vector<SensorEvent> buffer_;
    bool         is_online_ = true;
    uint64_t     monotonic_counter_ = 0;
    uint64_t     current_custody_seq_ = 0;
};

} // namespace coldchain

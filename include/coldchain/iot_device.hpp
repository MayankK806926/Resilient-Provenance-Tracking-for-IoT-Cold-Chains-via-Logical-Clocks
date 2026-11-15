#pragma once
/// @file iot_device.hpp
/// @brief Edge IoT sensor device with offline buffering.

#include <cstdint>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class IoTDevice {
public:
    IoTDevice(uint8_t entity_id, size_t n_nodes, uint32_t shipment_id);

    void set_online(bool online);
    bool is_online() const;

    SensorEvent generate_telemetry(float temperature, float lat, float lon,
                                   uint64_t sim_time_ns = 0,
                                   const DriftingPhysicalClock* phys_clock = nullptr);

    std::vector<SensorEvent> flush_on_reconnect();
    size_t buffered_count() const;

    const VectorClock& local_clock() const;
    VectorClock& local_clock_mut();

    uint8_t  entity_id()   const;
    uint32_t shipment_id() const;
    void     set_shipment_id(uint32_t id);

private:
    uint8_t  entity_id_;
    uint32_t shipment_id_;
    VectorClock local_vc_;
    std::vector<SensorEvent> buffer_;
    bool is_online_ = true;
    uint64_t monotonic_counter_ = 0;
};

} // namespace coldchain

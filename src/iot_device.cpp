/// @file iot_device.cpp
/// @brief IoT sensor logger implementation with offline buffering and signatures.

#include "coldchain/iot_device.hpp"

namespace coldchain {

IoTDevice::IoTDevice(uint32_t shipment_id, uint8_t initial_custodian, size_t n_nodes)
    : shipment_id_(shipment_id),
      current_custodian_(initial_custodian),
      n_nodes_(n_nodes),
      local_vc_(n_nodes),
      lamport_clock_(0) {}

void IoTDevice::set_online(bool online) {
    is_online_ = online;
}

bool IoTDevice::is_online() const {
    return is_online_;
}

void IoTDevice::set_current_custodian(uint8_t custodian_id) {
    current_custodian_ = custodian_id;
}

uint8_t IoTDevice::current_custodian() const {
    return current_custodian_;
}

SensorEvent IoTDevice::generate_telemetry(float temperature, float lat, float lon,
                                          uint64_t true_sim_time_ns,
                                          const DriftingPhysicalClock* phys_clock) {
    // Advance logical clock along the dimension of the current custodian
    local_vc_.tick(current_custodian_);
    uint64_t l_ts = lamport_clock_.tick();
    ++monotonic_counter_;

    uint64_t phys_ns = phys_clock ? phys_clock->now_ns(true_sim_time_ns, temperature)
                                  : true_sim_time_ns;

    SensorEvent ev;
    ev.shipment_id             = shipment_id_;
    ev.origin_entity_id        = current_custodian_;
    ev.temperature_celsius     = temperature;
    ev.latitude                = lat;
    ev.longitude               = lon;
    ev.local_monotonic_counter = monotonic_counter_;
    ev.vc                      = local_vc_;
    ev.lamport_ts              = l_ts;
    ev.creation_time_ns        = true_sim_time_ns;
    ev.physical_timestamp_ns   = phys_ns;
    ev.receipt_timestamp_ns    = 0;
    ev.is_pruned               = false;

    // Cryptographic signature with origin node's key
    ev.signature               = sign_event(ev.serialize(), node_private_key(current_custodian_));
    ev.id                      = ev.compute_hash();

    if (!is_online_) {
        buffer_.push_back(ev);
    }

    return ev;
}

std::vector<SensorEvent> IoTDevice::flush_on_reconnect() {
    std::vector<SensorEvent> flushed = std::move(buffer_);
    buffer_.clear();
    return flushed;
}

size_t IoTDevice::buffered_count() const {
    return buffer_.size();
}

const VectorClock& IoTDevice::local_clock() const {
    return local_vc_;
}

VectorClock& IoTDevice::local_clock_mut() {
    return local_vc_;
}

uint64_t IoTDevice::current_lamport_ts() const {
    return lamport_clock_.value();
}

void IoTDevice::merge_lamport_ts(uint64_t incoming_ts) {
    lamport_clock_.update_and_tick(incoming_ts);
}

uint32_t IoTDevice::shipment_id() const {
    return shipment_id_;
}

} // namespace coldchain

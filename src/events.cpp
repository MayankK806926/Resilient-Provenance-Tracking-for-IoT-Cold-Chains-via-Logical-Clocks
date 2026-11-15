/// @file events.cpp
/// @brief Serialization, hashing, and accessors for LedgerEvents.

#include "coldchain/events.hpp"
#include <iomanip>
#include <sstream>

namespace coldchain {

std::string SensorEvent::serialize() const {
    std::ostringstream os;
    os << "SE:" << shipment_id
       << ':' << static_cast<int>(origin_entity_id)
       << ':' << std::fixed << std::setprecision(2) << temperature_celsius
       << ':' << std::fixed << std::setprecision(4) << latitude
       << ':' << std::fixed << std::setprecision(4) << longitude
       << ':' << local_monotonic_counter
       << ':' << custody_transfer_seq
       << ':' << vc.to_string();
    return os.str();
}

Hash256 SensorEvent::compute_hash() const {
    return sha256_bytes(serialize());
}

std::string CustodyEvent::serialize() const {
    std::ostringstream os;
    os << "CE:" << shipment_id
       << ':' << static_cast<int>(from_entity_id)
       << ':' << static_cast<int>(to_entity_id)
       << ':' << custody_transfer_seq
       << ':' << last_sensor_seq
       << ':' << vc.to_string();
    return os.str();
}

Hash256 CustodyEvent::compute_hash() const {
    return sha256_bytes(serialize());
}

const VectorClock& event_vc(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> const VectorClock& { return e.vc; }, ev);
}

VectorClock& event_vc_mut(LedgerEvent& ev) {
    return std::visit([](auto& e) -> VectorClock& { return e.vc; }, ev);
}

uint8_t event_origin(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> uint8_t {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, SensorEvent>)
            return e.origin_entity_id;
        else
            return e.from_entity_id;
    }, ev);
}

uint64_t event_counter(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> uint64_t {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, SensorEvent>)
            return e.local_monotonic_counter;
        else
            return e.custody_transfer_seq;
    }, ev);
}

uint64_t event_physical_time(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.physical_timestamp_ns; }, ev);
}

uint64_t event_creation_time(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.creation_time_ns; }, ev);
}

uint64_t event_receipt_time(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.receipt_timestamp_ns; }, ev);
}

uint64_t event_lamport_ts(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.lamport_ts; }, ev);
}

uint32_t event_shipment_id(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.shipment_id; }, ev);
}

Hash256 event_hash(const LedgerEvent& ev) {
    return std::visit([](const auto& e) {
        if (e.is_pruned && e.id != Hash256{}) return e.id;
        return e.compute_hash();
    }, ev);
}

bool event_is_pruned(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.is_pruned; }, ev);
}

std::string event_serialize(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.serialize(); }, ev);
}

void set_event_receipt_time(LedgerEvent& ev, uint64_t ts_ns) {
    std::visit([ts_ns](auto& e) { e.receipt_timestamp_ns = ts_ns; }, ev);
}

void set_event_lamport_ts(LedgerEvent& ev, uint64_t ts) {
    std::visit([ts](auto& e) { e.lamport_ts = ts; }, ev);
}

void prune_event_payload(LedgerEvent& ev) {
    std::visit([](auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, SensorEvent>) {
            e.is_pruned = true;
            // Clear bulky payload readings while keeping metadata & cryptographic ID
            e.temperature_celsius = 0.0f;
            e.latitude = 0.0f;
            e.longitude = 0.0f;
        }
    }, ev);
}

} // namespace coldchain

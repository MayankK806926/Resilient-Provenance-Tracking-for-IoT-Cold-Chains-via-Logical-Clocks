#pragma once
/// @file events.hpp
/// @brief Event schemas for cold-chain ledger (SensorEvent, CustodyEvent) with hash identities.

#include <array>
#include <cstdint>
#include <string>
#include <variant>

#include "coldchain/hashing.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

struct SensorEvent {
    Hash256     id{};                     // Unique content hash of the event
    uint32_t    shipment_id;
    uint8_t     origin_entity_id;
    float       temperature_celsius;
    float       latitude;
    float       longitude;
    uint64_t    local_monotonic_counter;
    uint64_t    custody_transfer_seq = 0; // Custody sequence under which reading was generated
    VectorClock vc;
    uint64_t    lamport_ts = 0;           // For scalar Lamport baseline comparison
    Hash256     signature{};
    uint64_t    creation_time_ns = 0;     // Ground-truth generation time
    uint64_t    physical_timestamp_ns = 0;// Device RTC timestamp (with drift/offsets)
    uint64_t    receipt_timestamp_ns = 0; // Receipt timestamp at ledger node
    bool        is_pruned = false;        // True if telemetry payload was pruned for state GC

    std::string serialize() const;
    Hash256     compute_hash() const;
};

struct CustodyEvent {
    Hash256     id{};                     // Unique content hash of the event
    uint32_t    shipment_id = 0;
    uint8_t     from_entity_id = 0;
    uint8_t     to_entity_id = 0;
    uint8_t     origin_entity_id = 0;     // Custodian that emitted and signed this stage of handoff
    uint64_t    custody_transfer_seq = 0;
    uint64_t    last_sensor_seq = 0;      // Monotonic sensor counter at time of transfer
    VectorClock vc;
    uint64_t    lamport_ts = 0;
    Hash256     transfer_signature{};
    uint64_t    creation_time_ns = 0;
    uint64_t    physical_timestamp_ns = 0;
    uint64_t    receipt_timestamp_ns = 0;
    bool        is_pruned = false;        // Custody events are never pruned

    std::string serialize() const;
    Hash256     compute_hash() const;
};

using LedgerEvent = std::variant<SensorEvent, CustodyEvent>;

const VectorClock& event_vc(const LedgerEvent& ev);
VectorClock&       event_vc_mut(LedgerEvent& ev);
uint8_t            event_origin(const LedgerEvent& ev);
uint64_t           event_counter(const LedgerEvent& ev);
uint64_t           event_physical_time(const LedgerEvent& ev);
uint64_t           event_creation_time(const LedgerEvent& ev);
uint64_t           event_receipt_time(const LedgerEvent& ev);
uint64_t           event_lamport_ts(const LedgerEvent& ev);
uint32_t           event_shipment_id(const LedgerEvent& ev);
Hash256            event_hash(const LedgerEvent& ev);
bool               event_is_pruned(const LedgerEvent& ev);
std::string        event_serialize(const LedgerEvent& ev);

void set_event_receipt_time(LedgerEvent& ev, uint64_t ts_ns);
void set_event_lamport_ts(LedgerEvent& ev, uint64_t ts);
void prune_event_payload(LedgerEvent& ev);

} // namespace coldchain

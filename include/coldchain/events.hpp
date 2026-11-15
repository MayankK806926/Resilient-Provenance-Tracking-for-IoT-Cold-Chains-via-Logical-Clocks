#pragma once
/// @file events.hpp
/// @brief Event schemas for cold-chain ledger (SensorEvent, CustodyEvent).

#include <array>
#include <cstdint>
#include <string>
#include <variant>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

using Hash256 = std::array<uint8_t, 32>;

struct SensorEvent {
    uint32_t    shipment_id;
    uint8_t     origin_entity_id;
    float       temperature_celsius;
    float       latitude;
    float       longitude;
    uint64_t    local_monotonic_counter;
    VectorClock vc;
    Hash256     signature{};
    uint64_t    physical_timestamp_ns = 0;

    std::string serialize() const;
};

struct CustodyEvent {
    uint32_t    shipment_id;
    uint8_t     from_entity_id;
    uint8_t     to_entity_id;
    uint64_t    custody_transfer_seq;
    VectorClock vc;
    Hash256     transfer_signature{};
    uint64_t    physical_timestamp_ns = 0;

    std::string serialize() const;
};

using LedgerEvent = std::variant<SensorEvent, CustodyEvent>;

const VectorClock& event_vc(const LedgerEvent& ev);
uint8_t            event_origin(const LedgerEvent& ev);
uint64_t           event_counter(const LedgerEvent& ev);
uint64_t           event_physical_time(const LedgerEvent& ev);
uint32_t           event_shipment_id(const LedgerEvent& ev);
std::string        event_serialize(const LedgerEvent& ev);

} // namespace coldchain

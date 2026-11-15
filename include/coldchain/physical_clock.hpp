#pragma once
/// @file physical_clock.hpp
/// @brief Physical drifting clock models, Lamport logical clock, and baseline ordering engines.

#include <cstdint>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/events.hpp"

namespace coldchain {

class DriftingPhysicalClock {
public:
    DriftingPhysicalClock(double drift_ppm = 50.0,
                          int64_t initial_offset_ns = 0,
                          bool temp_dependent = true,
                          uint64_t seed = 42);

    // Compute drifted local time given true sim time in nanoseconds and current temperature
    uint64_t now_ns(uint64_t sim_time_ns, float current_temp_c = 4.0f) const;

    // Resynchronize clock upon network reconnection (models NTP / gateway sync)
    void resync(uint64_t sim_time_ns, int64_t residual_error_ns = 0);

    double drift_rate_ppm() const;
    int64_t current_offset_ns() const;

private:
    double   base_drift_ppm_;
    int64_t  offset_ns_;
    bool     temp_dependent_;
    uint64_t last_sync_sim_ns_ = 0;
};

// Lamport scalar logical clock state
class LamportClock {
public:
    explicit LamportClock(uint64_t initial = 0);
    uint64_t tick();
    uint64_t update_and_tick(uint64_t incoming_ts);
    uint64_t value() const;

private:
    uint64_t time_;
};

// Ordering baseline functions
std::vector<LedgerEvent> naive_receipt_order(std::vector<LedgerEvent> incoming);
std::vector<LedgerEvent> fair_creation_physical_order(std::vector<LedgerEvent> incoming);
std::vector<LedgerEvent> lamport_order(std::vector<LedgerEvent> incoming);

// Inversion and misordering checkers
size_t count_causal_inversions(const std::vector<LedgerEvent>& events);
bool   has_causal_inversion(const std::vector<LedgerEvent>& events);

} // namespace coldchain

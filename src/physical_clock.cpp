/// @file physical_clock.cpp
/// @brief Drifting physical clock simulation and baseline ordering implementations.

#include "coldchain/physical_clock.hpp"

#include <algorithm>
#include <cmath>

namespace coldchain {

DriftingPhysicalClock::DriftingPhysicalClock(double drift_ppm,
                                             int64_t initial_offset_ns,
                                             bool temp_dependent,
                                             uint64_t /*seed*/)
    : base_drift_ppm_(drift_ppm),
      offset_ns_(initial_offset_ns),
      temp_dependent_(temp_dependent) {}

uint64_t DriftingPhysicalClock::now_ns(uint64_t sim_time_ns, float current_temp_c) const {
    double elapsed_s = static_cast<double>(sim_time_ns - last_sync_sim_ns_) / 1e9;

    double effective_drift_ppm = base_drift_ppm_;
    if (temp_dependent_) {
        // Quartz crystal parabolic thermal drift: Δf/f = -0.035 ppm/°C² * (T - 25°C)²
        double temp_delta = current_temp_c - 25.0;
        double thermal_ppm = -0.035 * (temp_delta * temp_delta);
        effective_drift_ppm += thermal_ppm;
    }

    double accumulated_drift_ns = elapsed_s * (effective_drift_ppm * 1000.0);
    int64_t total_ns = static_cast<int64_t>(sim_time_ns) + offset_ns_ + static_cast<int64_t>(accumulated_drift_ns);
    return (total_ns > 0) ? static_cast<uint64_t>(total_ns) : 0;
}

void DriftingPhysicalClock::resync(uint64_t sim_time_ns, int64_t residual_error_ns) {
    last_sync_sim_ns_ = sim_time_ns;
    offset_ns_        = residual_error_ns;
}

double DriftingPhysicalClock::drift_rate_ppm() const {
    return base_drift_ppm_;
}

int64_t DriftingPhysicalClock::current_offset_ns() const {
    return offset_ns_;
}

// Lamport Clock implementation
LamportClock::LamportClock(uint64_t initial) : time_(initial) {}

uint64_t LamportClock::tick() {
    return ++time_;
}

uint64_t LamportClock::update_and_tick(uint64_t incoming_ts) {
    time_ = std::max(time_, incoming_ts) + 1;
    return time_;
}

uint64_t LamportClock::value() const {
    return time_;
}

// Baseline orderings
std::vector<LedgerEvent> naive_receipt_order(std::vector<LedgerEvent> incoming) {
    // Sort purely by receipt timestamp at gateway/ledger node
    std::stable_sort(incoming.begin(), incoming.end(),
                     [](const LedgerEvent& a, const LedgerEvent& b) {
                         uint64_t ra = event_receipt_time(a);
                         uint64_t rb = event_receipt_time(b);
                         if (ra != rb) return ra < rb;
                         return event_counter(a) < event_counter(b);
                     });
    return incoming;
}

std::vector<LedgerEvent> fair_creation_physical_order(std::vector<LedgerEvent> incoming) {
    // Fair physical baseline: sort by device's physical creation timestamp (subject to RTC drift)
    std::stable_sort(incoming.begin(), incoming.end(),
                     [](const LedgerEvent& a, const LedgerEvent& b) {
                         uint64_t pa = event_physical_time(a);
                         uint64_t pb = event_physical_time(b);
                         if (pa != pb) return pa < pb;
                         return event_counter(a) < event_counter(b);
                     });
    return incoming;
}

std::vector<LedgerEvent> lamport_order(std::vector<LedgerEvent> incoming) {
    // Lamport scalar clock ordering: sort by scalar timestamp, tie-break on origin
    std::stable_sort(incoming.begin(), incoming.end(),
                     [](const LedgerEvent& a, const LedgerEvent& b) {
                         uint64_t la = event_lamport_ts(a);
                         uint64_t lb = event_lamport_ts(b);
                         if (la != lb) return la < lb;
                         uint8_t oa = event_origin(a);
                         uint8_t ob = event_origin(b);
                         if (oa != ob) return oa < ob;
                         return event_counter(a) < event_counter(b);
                     });
    return incoming;
}

size_t count_causal_inversions(const std::vector<LedgerEvent>& events) {
    size_t inversions = 0;
    const size_t n = events.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = event_vc(events[i]);
        for (size_t j = i + 1; j < n; ++j) {
            if (event_vc(events[j]).precedes(vc_i)) {
                ++inversions;
            }
        }
    }
    return inversions;
}

bool has_causal_inversion(const std::vector<LedgerEvent>& events) {
    return count_causal_inversions(events) > 0;
}

} // namespace coldchain

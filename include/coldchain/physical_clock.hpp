#pragma once
/// @file physical_clock.hpp
/// @brief Drifting physical clock baseline for benchmark comparison.

#include <cstdint>
#include <vector>

#include "coldchain/events.hpp"

namespace coldchain {

class DriftingPhysicalClock {
public:
    DriftingPhysicalClock(double drift_ppm_mean = 20.0,
                          double drift_ppm_stddev = 10.0,
                          uint64_t seed = 42);

    uint64_t now_ns(uint64_t sim_time_ns) const;
    double drift_rate_ppm() const;

private:
    double drift_rate_ppm_;
};

std::vector<LedgerEvent> naive_physical_order(std::vector<LedgerEvent> incoming);
size_t count_causal_inversions(const std::vector<LedgerEvent>& events);
bool has_causal_inversion(const std::vector<LedgerEvent>& events);

} // namespace coldchain

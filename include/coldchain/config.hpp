#pragma once
/// @file config.hpp
/// @brief Simulation configuration parameters.

#include <cstdint>
#include <cstddef>

namespace coldchain {

inline constexpr size_t kDefaultNumNodes = 4;

inline constexpr uint8_t kManufacturer = 0;
inline constexpr uint8_t kDistributor  = 1;
inline constexpr uint8_t kColdStorage  = 2;
inline constexpr uint8_t kPharmacy     = 3;

struct SimConfig {
    size_t   num_nodes             = kDefaultNumNodes;
    uint32_t num_shipments         = 5;

    double   duration_hours        = 72.0;
    double   sampling_interval_s   = 60.0;

    double   blackout_mean_hours   = 4.0;
    double   blackout_stddev_hours = 2.0;
    double   blackout_rate_per_hour= 0.25;

    double   drift_ppm_mean        = 20.0; // PPM
    double   drift_ppm_stddev      = 10.0;

    double   tx_jitter_mean_ms     = 50.0;
    double   tx_jitter_stddev_ms   = 30.0;

    float    temp_low_c            =  2.0f;
    float    temp_high_c           =  8.0f;

    uint64_t seed                  = 42;
    size_t   events_per_block      = 32;
    size_t   gossip_rounds         = 3;
};

} // namespace coldchain

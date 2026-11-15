#pragma once
/// @file config.hpp
/// @brief Simulation and ledger configuration parameters.

#include <cstdint>
#include <cstddef>
#include <string>

namespace coldchain {

inline constexpr size_t kDefaultNumNodes = 4;
inline constexpr size_t kMaxNodes = 10;

inline constexpr uint8_t kManufacturer = 0;
inline constexpr uint8_t kDistributor  = 1;
inline constexpr uint8_t kColdStorage  = 2;
inline constexpr uint8_t kPharmacy     = 3;

enum class ColdChainProfile {
    StandardPharma_2_8C,   // 2.0°C to 8.0°C (vaccines, biologics, insulin)
    UltraCold_minus70C     // -80.0°C to -60.0°C (mRNA vaccines, e.g. Pfizer-BioNTech)
};

struct SimConfig {
    size_t   num_nodes             = kDefaultNumNodes;
    uint32_t num_shipments         = 5;

    double   duration_hours        = 72.0;
    double   sampling_interval_s   = 60.0;

    // Device-to-gateway blackout failures
    double   blackout_mean_hours   = 4.0;
    double   blackout_stddev_hours = 2.0;
    double   blackout_rate_per_hour= 0.25;

    // Node-to-node replication partition and loss
    double   partition_rate_per_hour  = 0.10;
    double   partition_mean_hours     = 2.0;
    double   link_loss_probability    = 0.0;

    // Physical clock model: ~100 ms/hr (~28 ppm) to ~10 s/day (~116 ppm)
    double   drift_ppm_min         = 27.78;
    double   drift_ppm_max         = 115.74;
    double   drift_ppm_mean        = 50.0;
    double   drift_ppm_stddev      = 20.0;
    double   initial_offset_max_s  = 5.0;
    bool     temp_dependent_drift  = true;
    bool     resync_at_reconnect   = true;
    // If true, a gateway/device overwrites buffered readings' physical timestamps with the
    // upload (reconnect) time. Default false: buffered readings keep their creation-time RTC stamp.
    bool     restamp_physical_on_upload = false;

    // Network transmission jitter
    double   tx_jitter_mean_ms     = 50.0;
    double   tx_jitter_stddev_ms   = 30.0;

    // Cold-chain profile and temperature limits
    ColdChainProfile profile       = ColdChainProfile::StandardPharma_2_8C;
    float    temp_low_c            = 2.0f;
    float    temp_high_c           = 8.0f;

    // Contiguous excursion episode model
    double   excursion_rate_per_hour   = 0.08; // Poisson rate of excursion start
    double   excursion_duration_mean_min = 20.0; // Mean duration of excursion episode

    // Shipment dynamics
    double   stagger_start_max_hours = 4.0;   // Staggered departure times
    bool     randomize_leg_lengths   = true;  // Varied leg travel durations

    // Replicated ledger & matrix pruning configuration
    uint64_t seed                  = 42;
    size_t   events_per_block      = 32;
    size_t   gossip_rounds         = 3;
    double   gossip_interval_s     = 300.0;
    double   handoff_delay_mean_ms = 100.0;

    // Ablation toggles
    bool     hold_back_enabled          = true;
    bool     handoff_merge_enabled      = true;
    bool     verify_signatures_enabled  = true;

    void apply_profile(ColdChainProfile p) {
        profile = p;
        if (p == ColdChainProfile::StandardPharma_2_8C) {
            temp_low_c  = 2.0f;
            temp_high_c = 8.0f;
        } else if (p == ColdChainProfile::UltraCold_minus70C) {
            temp_low_c  = -80.0f;
            temp_high_c = -60.0f;
        }
    }
};

} // namespace coldchain

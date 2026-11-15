#pragma once
/// @file metrics.hpp
/// @brief Multi-pipeline metrics computation (R_err with dual denominators, F_liab, C_err, S_prune).

#include <cstddef>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"

namespace coldchain {

class NetworkSimulator;

struct PipelineMetrics {
    double r_err_causal           = 0.0; // Inversions / causally-related pairs
    double r_err_total            = 0.0; // Inversions / all pairs
    double f_liab                 = 0.0; // False liability attribution rate (-1.0 if undefined)
    bool   f_liab_defined         = false;
    double c_err_concurrency      = 0.0; // Stored order inversions of concurrent pairs
    size_t inversions             = 0;
    size_t total_excursions       = 0;
    size_t misattributed_excursions = 0;
    size_t masked_excursions      = 0;
    size_t ambiguous_excursions   = 0;
};

struct RunMetrics {
    // Pipeline comparisons
    PipelineMetrics vclock;          // Vector clock + causal merge engine
    PipelineMetrics fair_physical;   // Fair creation-time drifting physical RTC
    PipelineMetrics naive_receipt;   // Naive receipt-time arrival order
    PipelineMetrics lamport;         // Lamport scalar clock

    // Matrix pruning & storage metrics
    double storage_reduction_ratio = 0.0; // S_prune
    size_t unpruned_store_size     = 0;
    size_t pruned_store_size       = 0;
    size_t safety_violations       = 0;   // Oracle safety invariant violations (must be 0)
    size_t gossip_messages         = 0;
    size_t gossip_bytes            = 0;

    // Simulation metadata
    double avg_blackout_hours      = 0.0;
    double sampling_interval_s     = 60.0;
    double drift_ppm_mean          = 50.0;
    size_t total_events            = 0;
    size_t total_excursions        = 0;
    size_t total_blocks            = 0;
    bool   all_replicas_converged  = false;
};

struct AggregateMetrics {
    double mean;
    double stddev;
    double ci95_low;
    double ci95_high;
};

AggregateMetrics compute_ci95(const std::vector<double>& values);

RunMetrics evaluate_simulation(NetworkSimulator& sim, const SimConfig& cfg);
RunMetrics run_single_experiment(const SimConfig& cfg);

std::string metrics_csv_header();
std::string metrics_to_csv_row(const RunMetrics& m, uint64_t run_id);

} // namespace coldchain

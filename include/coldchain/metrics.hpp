#pragma once
/// @file metrics.hpp
/// @brief Metrics computation (R_err, F_liab, S_prune).

#include <cstddef>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/config.hpp"

namespace coldchain {

class LedgerNode;
class NetworkSimulator;

struct RunMetrics {
    double causal_misordering_rate = 0.0; // R_err
    double false_liability_rate    = 0.0; // F_liab
    double storage_reduction_ratio = 0.0; // S_prune
    double avg_blackout_hours      = 0.0;
    size_t naive_inversions        = 0;
    size_t vclock_inversions       = 0;
    size_t total_events            = 0;
    size_t total_excursions        = 0;
    size_t false_liability_count   = 0;
    size_t unpruned_store_size     = 0;
    size_t pruned_store_size       = 0;
};

struct ExcursionRecord {
    uint32_t shipment_id;
    uint8_t  true_custodian;
    float    temperature_celsius;
    VectorClock vc;
    uint64_t physical_timestamp_ns;
};

double compute_causal_misordering_rate(
    const std::vector<LedgerEvent>& ordered_events,
    size_t& out_inversion_count);

double compute_false_liability_rate(
    const std::vector<ExcursionRecord>& excursions,
    const std::vector<LedgerEvent>& naive_events,
    const SimConfig& cfg);

double compute_storage_reduction(size_t pruned_size, size_t unbounded_size);

std::vector<ExcursionRecord> extract_excursions(
    const std::vector<LedgerEvent>& ground_truth,
    const SimConfig& cfg);

RunMetrics run_single_experiment(const SimConfig& cfg);
std::string metrics_to_csv_row(const RunMetrics& m, uint64_t run_id);
std::string metrics_csv_header();

} // namespace coldchain

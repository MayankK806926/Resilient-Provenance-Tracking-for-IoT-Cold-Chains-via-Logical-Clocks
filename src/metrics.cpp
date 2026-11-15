/// @file metrics.cpp
/// @brief Multi-pipeline metrics computation (R_err, F_liab, C_err, S_prune).

#include "coldchain/metrics.hpp"
#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/pruning_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace coldchain {

namespace {

uint8_t find_true_custodian(uint64_t true_time_ns,
                            uint32_t shipment_id,
                            const std::vector<CustodyInterval>& intervals) {
    double time_ms = static_cast<double>(true_time_ns) / 1e6;
    for (const auto& ci : intervals) {
        if (ci.shipment_id == shipment_id && time_ms >= ci.start_ms && time_ms < ci.end_ms) {
            return ci.custodian_id;
        }
    }
    return 0; // Default origin
}

PipelineMetrics evaluate_event_sequence(const std::vector<LedgerEvent>& events,
                                        const NetworkSimulator& sim,
                                        const SimConfig& cfg) {
    PipelineMetrics pm;
    const size_t n = events.size();
    if (n <= 1) return pm;

    // Map each event's hash to its ground-truth index
    const auto& gt_events = sim.ground_truth_events();
    std::map<Hash256, size_t> hash_to_gt;
    for (size_t i = 0; i < gt_events.size(); ++i) {
        hash_to_gt[event_hash(gt_events[i])] = i;
    }

    std::vector<size_t> gt_indices(n, SIZE_MAX);
    for (size_t i = 0; i < n; ++i) {
        auto it = hash_to_gt.find(event_hash(events[i]));
        if (it != hash_to_gt.end()) {
            gt_indices[i] = it->second;
        }
    }

    size_t inversions = 0;
    size_t causally_related_pairs = 0;
    size_t concurrent_pairs = 0;
    size_t concurrent_inversions = 0;
    size_t total_pairs = 0;

    for (size_t i = 0; i < n; ++i) {
        size_t gt_i = gt_indices[i];
        if (gt_i == SIZE_MAX) continue;

        for (size_t j = i + 1; j < n; ++j) {
            size_t gt_j = gt_indices[j];
            if (gt_j == SIZE_MAX) continue;

            ++total_pairs;

            bool j_precedes_i = sim.is_ground_truth_preceding(gt_j, gt_i);
            bool i_precedes_j = sim.is_ground_truth_preceding(gt_i, gt_j);

            if (j_precedes_i) {
                // Inversion: event j happened before event i in ground truth,
                // but event i is positioned before event j in stored order!
                ++inversions;
                ++causally_related_pairs;
            } else if (i_precedes_j) {
                ++causally_related_pairs;
            } else {
                // Concurrent in ground truth: neither causally precedes the other
                ++concurrent_pairs;
                // Check if stored order (i before j) inverts true physical creation time
                if (event_creation_time(events[j]) < event_creation_time(events[i])) {
                    ++concurrent_inversions;
                }
            }
        }
    }

    pm.inversions = inversions;
    pm.r_err_causal = (causally_related_pairs > 0)
        ? static_cast<double>(inversions) / static_cast<double>(causally_related_pairs)
        : 0.0;
    pm.r_err_total = (total_pairs > 0)
        ? static_cast<double>(inversions) / static_cast<double>(total_pairs)
        : 0.0;

    // Concurrency misordering metric: fraction of concurrent pairs where stored order contradicts true creation order
    pm.c_err_concurrency = (concurrent_pairs > 0)
        ? static_cast<double>(concurrent_inversions) / static_cast<double>(concurrent_pairs)
        : 0.0;

    // Evaluate F_liab (False liability attribution)
    std::map<uint32_t, uint8_t> apparent_custody;
    const auto& intervals = sim.true_custody_intervals();
    size_t total_excursions = 0;

    for (const auto& ev : events) {
        if (const auto* ce = std::get_if<CustodyEvent>(&ev)) {
            apparent_custody[ce->shipment_id] = ce->to_entity_id;
        } else if (const auto* se = std::get_if<SensorEvent>(&ev)) {
            // Use the ORIGINAL reading from ground truth: stored payloads may have been pruned (zeroed).
            float temp = se->temperature_celsius;
            auto git = hash_to_gt.find(event_hash(ev));
            if (git != hash_to_gt.end()) {
                if (const auto* g = std::get_if<SensorEvent>(&gt_events[git->second])) temp = g->temperature_celsius;
            }
            if (temp < cfg.temp_low_c || temp > cfg.temp_high_c) {
                ++total_excursions;

                uint8_t true_custodian = find_true_custodian(se->creation_time_ns, se->shipment_id, intervals);
                uint8_t apparent = apparent_custody.count(se->shipment_id)
                                 ? apparent_custody[se->shipment_id]
                                 : kManufacturer;

                // Check handoff ambiguity window: if excursion happened within 5 minutes of a custody change
                double time_ms = static_cast<double>(se->creation_time_ns) / 1e6;
                bool near_handoff = false;
                for (const auto& ci : intervals) {
                    if (ci.shipment_id == se->shipment_id) {
                        if (std::abs(time_ms - ci.start_ms) < 300'000.0 ||
                            std::abs(time_ms - ci.end_ms) < 300'000.0) {
                            near_handoff = true;
                            break;
                        }
                    }
                }

                if (near_handoff && apparent != true_custodian) {
                    ++pm.ambiguous_excursions;
                }

                if (apparent != true_custodian) {
                    ++pm.misattributed_excursions;
                }
            }
        }
    }

    pm.total_excursions = total_excursions;
    if (total_excursions > 0) {
        pm.f_liab_defined = true;
        pm.f_liab = static_cast<double>(pm.misattributed_excursions) / static_cast<double>(total_excursions);
    } else {
        pm.f_liab_defined = false;
        pm.f_liab = -1.0; // Undefined when no excursions occurred
    }

    return pm;
}

} // namespace

AggregateMetrics compute_ci95(const std::vector<double>& values) {
    AggregateMetrics res{0.0, 0.0, 0.0, 0.0};
    if (values.empty()) return res;

    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    res.mean = sum / static_cast<double>(values.size());

    if (values.size() > 1) {
        double sq_sum = 0.0;
        for (double v : values) {
            sq_sum += (v - res.mean) * (v - res.mean);
        }
        res.stddev = std::sqrt(sq_sum / static_cast<double>(values.size() - 1));
        double margin = 1.96 * (res.stddev / std::sqrt(static_cast<double>(values.size())));
        res.ci95_low  = res.mean - margin;
        res.ci95_high = res.mean + margin;
    } else {
        res.ci95_low  = res.mean;
        res.ci95_high = res.mean;
    }
    return res;
}

RunMetrics evaluate_simulation(NetworkSimulator& sim, const SimConfig& cfg) {
    RunMetrics rm;
    rm.avg_blackout_hours  = cfg.blackout_mean_hours;
    rm.sampling_interval_s = cfg.sampling_interval_s;
    rm.drift_ppm_mean      = cfg.drift_ppm_mean;
    rm.total_events        = sim.total_events_generated();

    // 1. Vector Clock pipeline stored order (replica 0 stored chain & events)
    const auto& node0_events = sim.vclock_node(0).all_events();
    rm.vclock = evaluate_event_sequence(node0_events, sim, cfg);

    // 2. Convergence across all replicas
    rm.all_replicas_converged = true;
    if (sim.num_nodes() > 1) {
        Hash256 ref_hash = sim.vclock_node(0).canonical_state_hash();
        for (size_t i = 1; i < sim.num_nodes(); ++i) {
            if (sim.vclock_node(i).canonical_state_hash() != ref_hash) {
                rm.all_replicas_converged = false;
                break;
            }
        }
    }

    // 3. Baselines evaluated on the EXACT SAME delivered event set as the VC pipeline (replica 0)
    auto fair_phys_order = fair_creation_physical_order(node0_events);
    rm.fair_physical     = evaluate_event_sequence(fair_phys_order, sim, cfg);

    auto naive_rec_order = naive_receipt_order(node0_events);
    rm.naive_receipt     = evaluate_event_sequence(naive_rec_order, sim, cfg);

    auto lamport_ord     = lamport_order(node0_events);
    rm.lamport           = evaluate_event_sequence(lamport_ord, sim, cfg);

    // 4. Matrix clock gossip and pruning evaluation
    if (cfg.gossip_rounds > 0) {
        sim.gossip_rounds(cfg.gossip_rounds);
    }
    rm.gossip_messages = sim.gossip_messages_sent();
    rm.gossip_bytes    = sim.gossip_bytes_sent();

    size_t unpruned_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        unpruned_total += sim.vclock_node(i).active_state_store().size();
    }
    rm.unpruned_store_size = unpruned_total;

    // Execute pruning with safety invariant verification oracle
    size_t safety_violations = 0;
    size_t pruned_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        PruningEngine::prune_stable_records(
            sim.vclock_node(i),
            cfg.temp_low_c,
            cfg.temp_high_c,
            &sim.all_vclock_nodes(),
            &safety_violations
        );
        pruned_total += sim.vclock_node(i).active_state_store().size();
    }

    rm.pruned_store_size   = pruned_total;
    rm.safety_violations   = sim.safety_violations_detected() + safety_violations;
    rm.storage_reduction_ratio = (unpruned_total > 0)
        ? (1.0 - static_cast<double>(pruned_total) / static_cast<double>(unpruned_total))
        : 1.0;

    size_t blks = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        blks += sim.vclock_node(i).chain().size();
    }
    rm.total_blocks = blks;

    return rm;
}

RunMetrics run_single_experiment(const SimConfig& cfg) {
    NetworkSimulator sim(cfg);
    sim.run_full();
    return evaluate_simulation(sim, cfg);
}

std::string metrics_csv_header() {
    return "run_id,blackout_hours,sampling_interval_s,drift_ppm,"
           "vc_r_err_causal,vc_r_err_total,vc_f_liab,vc_c_err,"
           "fair_r_err_causal,fair_r_err_total,fair_f_liab,fair_c_err,"
           "naive_r_err_causal,naive_r_err_total,naive_f_liab,naive_c_err,"
           "lamport_r_err_causal,lamport_r_err_total,lamport_f_liab,lamport_c_err,"
           "s_prune,safety_violations,gossip_msgs,gossip_bytes,converged";
}

std::string metrics_to_csv_row(const RunMetrics& m, uint64_t run_id) {
    std::ostringstream oss;
    oss << run_id << ","
        << std::fixed << std::setprecision(4)
        << m.avg_blackout_hours << ","
        << m.sampling_interval_s << ","
        << m.drift_ppm_mean << ","
        << m.vclock.r_err_causal << ","
        << m.vclock.r_err_total << ","
        << m.vclock.f_liab << ","
        << m.vclock.c_err_concurrency << ","
        << m.fair_physical.r_err_causal << ","
        << m.fair_physical.r_err_total << ","
        << m.fair_physical.f_liab << ","
        << m.fair_physical.c_err_concurrency << ","
        << m.naive_receipt.r_err_causal << ","
        << m.naive_receipt.r_err_total << ","
        << m.naive_receipt.f_liab << ","
        << m.naive_receipt.c_err_concurrency << ","
        << m.lamport.r_err_causal << ","
        << m.lamport.r_err_total << ","
        << m.lamport.f_liab << ","
        << m.lamport.c_err_concurrency << ","
        << m.storage_reduction_ratio << ","
        << m.safety_violations << ","
        << m.gossip_messages << ","
        << m.gossip_bytes << ","
        << (m.all_replicas_converged ? "1" : "0");
    return oss.str();
}

} // namespace coldchain

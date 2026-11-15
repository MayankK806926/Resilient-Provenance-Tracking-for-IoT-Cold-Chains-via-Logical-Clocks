/// @file main_sim.cpp
/// @brief CLI simulation runner — multi-pipeline comparison (Vector Clock vs Fair Physical vs Naive vs Lamport),
///        Merkle multi-block chain verification, and matrix-clock pruning.

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/metrics.hpp"
#include "coldchain/network_simulator.hpp"

using namespace coldchain;

void print_banner() {
    std::cout << "================================================================================\n"
              << " Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks\n"
              << " Multi-Pipeline Discrete-Event Simulation & Replicated Ledger Verification\n"
              << "================================================================================\n\n";
}

int main(int argc, char* argv[]) {
    SimConfig cfg;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shipments" && i + 1 < argc) {
            cfg.num_shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-hours" && i + 1 < argc) {
            cfg.duration_hours = std::stod(argv[++i]);
        } else if (arg == "--blackout-mean-hours" && i + 1 < argc) {
            cfg.blackout_mean_hours = std::stod(argv[++i]);
        } else if (arg == "--sampling-interval-s" && i + 1 < argc) {
            cfg.sampling_interval_s = std::stod(argv[++i]);
        } else if (arg == "--gossip-rounds" && i + 1 < argc) {
            cfg.gossip_rounds = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--ultra-cold") {
            cfg.apply_profile(ColdChainProfile::UltraCold_minus70C);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./sim [options]\n"
                      << "Options:\n"
                      << "  --shipments N              Number of shipments (default: 5)\n"
                      << "  --duration-hours H         Simulation duration in hours (default: 72)\n"
                      << "  --blackout-mean-hours H    Mean blackout duration (default: 4)\n"
                      << "  --sampling-interval-s S    Telemetry interval in seconds (default: 60)\n"
                      << "  --gossip-rounds N          Matrix clock gossip rounds (default: 3)\n"
                      << "  --ultra-cold               Use -70°C mRNA vaccine profile\n"
                      << "  --seed S                   Random seed (default: 42)\n"
                      << "  --verbose, -v              Print detailed logs\n"
                      << "  --help, -h                 Show this message\n";
            return 0;
        }
    }

    print_banner();
    std::cout << "[Configuration]\n"
              << "  Shipments:          " << cfg.num_shipments << "\n"
              << "  Duration:           " << cfg.duration_hours << " hours\n"
              << "  Sampling interval:  " << cfg.sampling_interval_s << " s\n"
              << "  Profile:            " << (cfg.profile == ColdChainProfile::StandardPharma_2_8C ? "2-8°C Standard Pharma" : "-70°C Ultra-Cold mRNA") << "\n"
              << "  Safe Temp Limits:   [" << cfg.temp_low_c << "°C, " << cfg.temp_high_c << "°C]\n"
              << "  Clock Drift Range:  [" << cfg.drift_ppm_min << ", " << cfg.drift_ppm_max << "] PPM\n"
              << "  Replication Nodes:  " << cfg.num_nodes << "\n"
              << "  Block Capacity:     " << cfg.events_per_block << " events/block\n"
              << "  Gossip Rounds:      " << cfg.gossip_rounds << "\n"
              << "  Random Seed:        " << cfg.seed << "\n\n";

    std::cout << "[Simulation] Running discrete-event cold-chain execution...\n";
    NetworkSimulator sim(cfg);
    sim.run_full();

    std::cout << "  Events generated:   " << sim.total_events_generated() << "\n"
              << "  Blackouts injected: " << sim.total_blackouts_injected() << "\n"
              << "  Partitions tested:  " << sim.total_partitions_injected() << "\n\n";

    std::cout << "[Evaluation] Computing metrics across baselines and replicas...\n";
    RunMetrics m = evaluate_simulation(sim, cfg);

    std::cout << "\n--------------------------------------------------------------------------------\n"
              << " EVALUATION RESULTS: 4-PIPELINE COMPARATIVE BENCHMARK\n"
              << "--------------------------------------------------------------------------------\n";

    std::cout << std::left
              << std::setw(28) << "Metric"
              << " | " << std::setw(11) << "VectorClock"
              << " | " << std::setw(12) << "FairPhysical"
              << " | " << std::setw(12) << "NaiveReceipt"
              << " | " << std::setw(11) << "Lamport" << "\n";
    std::cout << "-----------------------------+-------------+--------------+--------------+-------------\n";

    auto print_row_d = [](const std::string& name, double vc, double fp, double nr, double lp, int prec = 4) {
        std::cout << std::left << std::setw(28) << name << " | "
                  << std::right << std::fixed << std::setprecision(prec)
                  << std::setw(11) << vc << " | "
                  << std::setw(12) << fp << " | "
                  << std::setw(12) << nr << " | "
                  << std::setw(11) << lp << "\n";
    };

    auto print_row_u = [](const std::string& name, size_t vc, size_t fp, size_t nr, size_t lp) {
        std::cout << std::left << std::setw(28) << name << " | "
                  << std::right
                  << std::setw(11) << vc << " | "
                  << std::setw(12) << fp << " | "
                  << std::setw(12) << nr << " | "
                  << std::setw(11) << lp << "\n";
    };

    print_row_u("Causal Inversions (Count)",
                m.vclock.inversions, m.fair_physical.inversions, m.naive_receipt.inversions, m.lamport.inversions);
    print_row_d("R_err (Causal Denom)",
                m.vclock.r_err_causal, m.fair_physical.r_err_causal, m.naive_receipt.r_err_causal, m.lamport.r_err_causal, 6);
    print_row_d("R_err (Total Denom)",
                m.vclock.r_err_total, m.fair_physical.r_err_total, m.naive_receipt.r_err_total, m.lamport.r_err_total, 6);
    if (m.vclock.f_liab_defined) {
        print_row_d("False Liability (F_liab)",
                    m.vclock.f_liab, m.fair_physical.f_liab, m.naive_receipt.f_liab, m.lamport.f_liab, 4);
    } else {
        std::cout << std::left << std::setw(28) << "False Liability (F_liab)" << " | "
                  << std::setw(11) << "N/A" << " | "
                  << std::setw(12) << "N/A" << " | "
                  << std::setw(12) << "N/A" << " | "
                  << std::setw(11) << "N/A" << " (0 excursions in run)\n";
    }
    print_row_d("Concurrent Misordering C_err",
                m.vclock.c_err_concurrency, m.fair_physical.c_err_concurrency, m.naive_receipt.c_err_concurrency, m.lamport.c_err_concurrency, 4);

    std::cout << "-----------------------------+-------------+--------------+--------------+-------------\n";
    std::cout << "  Excursion samples evaluated (ground-truth readings): " << m.vclock.total_excursions << "\n";

    std::cout << "\n[Replicated Ledger Verification]\n"
              << "  Replica State Convergence:  " << (m.all_replicas_converged ? "CONVERGED (Identical canonical state hash)" : "FAILED (Divergent)") << "\n";

    bool all_chains_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.vclock_node(i).chain())) {
            all_chains_valid = false;
            break;
        }
    }
    std::cout << "  Multi-block Chain Integrity: " << (all_chains_valid ? "VALID (All block hashes & Merkle roots verified)" : "CORRUPT") << "\n"
              << "  Total Blocks Sealed:         " << m.total_blocks << "\n";

    std::cout << "\n[Matrix-Clock Pruning & Garbage Collection]\n"
              << "  Store Size (Unpruned):       " << m.unpruned_store_size << " records\n"
              << "  Store Size (Pruned):         " << m.pruned_store_size << " records\n"
              << "  Storage Reduction (S_prune): " << std::fixed << std::setprecision(2) << (m.storage_reduction_ratio * 100.0) << "%\n"
              << "  Oracle Safety Violations:    " << m.safety_violations << " (must be 0)\n"
              << "  Gossip Network Overhead:     " << m.gossip_messages << " messages (" << m.gossip_bytes << " bytes)\n";

    if (verbose) {
        std::cout << "\n[Per-Node Matrix Horizons]\n";
        for (size_t i = 0; i < sim.num_nodes(); ++i) {
            std::cout << "  Node " << i << " Pruning Horizon: "
                      << sim.vclock_node(i).matrix_clock().column_min().to_string() << "\n";
        }
    }

    std::cout << "\n================================================================================\n";
    return 0;
}

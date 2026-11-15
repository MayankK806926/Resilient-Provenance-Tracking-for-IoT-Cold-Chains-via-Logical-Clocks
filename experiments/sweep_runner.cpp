/// @file sweep_runner.cpp
/// @brief Multi-parameter statistical sweep runner with 95% CI evaluation and CSV output.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/metrics.hpp"

using namespace coldchain;

static std::vector<double> parse_doubles(const std::string& s) {
    std::vector<double> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        result.push_back(std::stod(token));
    }
    return result;
}

int main(int argc, char* argv[]) {
    std::vector<double> blackout_hours = {0.1, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 24.0, 48.0};
    size_t runs_per_point = 30; // Report requirement: at least 30 seeds
    std::string output_path = "data/sweeps/results.csv";
    std::string sweep_mode = "blackout"; // "blackout", "drift", "gossip", "scale", "ablation"
    SimConfig base_cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--blackout-hours" && i + 1 < argc) {
            blackout_hours = parse_doubles(argv[++i]);
        } else if (arg == "--runs-per-point" && i + 1 < argc) {
            runs_per_point = std::stoul(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            sweep_mode = argv[++i];
        } else if (arg == "--shipments" && i + 1 < argc) {
            base_cfg.num_shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-hours" && i + 1 < argc) {
            base_cfg.duration_hours = std::stod(argv[++i]);
        } else if (arg == "--base-seed" && i + 1 < argc) {
            base_cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./sweep_runner [options]\n"
                      << "Options:\n"
                      << "  --blackout-hours H1,H2,...   Blackout durations to sweep\n"
                      << "  --runs-per-point N           Repetitions per setting (default: 30)\n"
                      << "  --mode MODE                  Sweep mode: blackout, drift, gossip, scale, ablation\n"
                      << "  --out PATH                   CSV output path (default: data/sweeps/results.csv)\n"
                      << "  --shipments N                Number of shipments\n"
                      << "  --duration-hours H           Duration in hours\n"
                      << "  --base-seed S                Base random seed\n"
                      << "  --help, -h                   Show this message\n";
            return 0;
        }
    }

    std::filesystem::path out_p(output_path);
    if (out_p.has_parent_path()) {
        std::filesystem::create_directories(out_p.parent_path());
    }

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        std::cerr << "ERROR: cannot open output file: " << output_path << "\n";
        return 1;
    }

    ofs << metrics_csv_header() << "\n";

    std::cout << "------------------------------------------------------------\n"
              << " Cold Chain Statistical Sweep Runner (Mode: " << sweep_mode << ")\n"
              << " Repetitions per parameter point: " << runs_per_point << "\n"
              << " Output: " << output_path << "\n"
              << "------------------------------------------------------------\n";

    uint64_t run_counter = 0;

    if (sweep_mode == "blackout") {
        for (double b_hr : blackout_hours) {
            std::cout << ">> Running blackout duration: " << b_hr << " hours...\n";
            std::vector<double> vc_r_errs, fair_r_errs, s_prunes;

            for (size_t rep = 0; rep < runs_per_point; ++rep) {
                SimConfig cfg = base_cfg;
                cfg.blackout_mean_hours = b_hr;
                cfg.seed = base_cfg.seed + run_counter * 17 + rep;

                RunMetrics rm = run_single_experiment(cfg);
                ofs << metrics_to_csv_row(rm, ++run_counter) << "\n";
                ofs.flush();

                vc_r_errs.push_back(rm.vclock.r_err_causal);
                fair_r_errs.push_back(rm.fair_physical.r_err_causal);
                s_prunes.push_back(rm.storage_reduction_ratio);
            }

            auto ci_vc = compute_ci95(vc_r_errs);
            auto ci_fp = compute_ci95(fair_r_errs);
            auto ci_sp = compute_ci95(s_prunes);

            std::cout << "   VectorClock R_err: " << std::fixed << std::setprecision(6)
                      << ci_vc.mean << " [95% CI: " << ci_vc.ci95_low << ", " << ci_vc.ci95_high << "]\n"
                      << "   FairPhysical R_err: " << ci_fp.mean << " [95% CI: " << ci_fp.ci95_low << ", " << ci_fp.ci95_high << "]\n"
                      << "   S_prune: " << std::setprecision(2) << (ci_sp.mean * 100.0) << "%\n";
        }
    } else if (sweep_mode == "drift") {
        // Drift sweep from 100 ms/hr (~27.8 ppm) to 10 s/day (~115.7 ppm)
        std::vector<double> drift_ppms = {10.0, 27.78, 50.0, 75.0, 100.0, 115.74, 150.0};
        for (double drift : drift_ppms) {
            std::cout << ">> Running drift sweep: " << drift << " PPM...\n";
            for (size_t rep = 0; rep < runs_per_point; ++rep) {
                SimConfig cfg = base_cfg;
                cfg.drift_ppm_mean = drift;
                cfg.drift_ppm_min  = drift * 0.8;
                cfg.drift_ppm_max  = drift * 1.2;
                cfg.seed = base_cfg.seed + run_counter * 17 + rep;

                RunMetrics rm = run_single_experiment(cfg);
                ofs << metrics_to_csv_row(rm, ++run_counter) << "\n";
                ofs.flush();
            }
        }
    } else if (sweep_mode == "ablation") {
        std::cout << ">> Running ablation suite...\n";
        // 1. Full pipeline
        // 2. No hold-back queue
        // 3. No handoff merge
        // 4. No signature check
        struct AblationSetting {
            std::string name;
            bool hold_back;
            bool handoff_merge;
            bool verify_sig;
        };
        std::vector<AblationSetting> ablations = {
            {"Full_Pipeline", true, true, true},
            {"No_HoldBackQueue", false, true, true},
            {"No_HandoffMerge", true, false, true},
            {"No_SignatureCheck", true, true, false}
        };

        for (const auto& ab : ablations) {
            std::cout << "   Ablation: " << ab.name << "\n";
            for (size_t rep = 0; rep < runs_per_point; ++rep) {
                SimConfig cfg = base_cfg;
                cfg.hold_back_enabled = ab.hold_back;
                cfg.handoff_merge_enabled = ab.handoff_merge;
                cfg.verify_signatures_enabled = ab.verify_sig;
                cfg.seed = base_cfg.seed + run_counter * 17 + rep;

                RunMetrics rm = run_single_experiment(cfg);
                ofs << metrics_to_csv_row(rm, ++run_counter) << "\n";
                ofs.flush();
            }
        }
    }

    std::cout << "\nSweep completed successfully. Results saved to: " << output_path << "\n";
    return 0;
}

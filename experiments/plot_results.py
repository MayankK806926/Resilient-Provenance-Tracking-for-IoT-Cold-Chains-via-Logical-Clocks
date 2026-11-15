#!/usr/bin/env python3
"""
plot_results.py — Generates publication-ready plots comparing Vector Clock
vs Fair Physical vs Naive vs Lamport baselines.
"""

import argparse
import sys
from pathlib import Path

def plot_sweeps(input_csv: Path, output_dir: Path):
    try:
        import pandas as pd
        import matplotlib.pyplot as plt
    except ImportError:
        print("WARNING: pandas or matplotlib not installed. Skipping plot rendering.")
        return

    output_dir.mkdir(parents=True, exist_ok=True)
    df = pd.read_csv(input_csv)

    # Group by blackout_hours and compute mean and std
    if "blackout_hours" in df.columns:
        grouped = df.groupby("blackout_hours").mean(numeric_only=True)
        std = df.groupby("blackout_hours").std(numeric_only=True)

        # Plot 1: R_err comparison across pipelines
        plt.figure(figsize=(8, 5))
        if "vc_r_err_causal" in grouped.columns:
            plt.plot(grouped.index, grouped["vc_r_err_causal"], 'o-', label="Vector Clock (Guaranteed 0)", color="#2ca02c", linewidth=2.5)
        if "fair_r_err_causal" in grouped.columns:
            plt.plot(grouped.index, grouped["fair_r_err_causal"], 's--', label="Fair Physical (Creation RTC)", color="#1f77b4")
        if "naive_r_err_causal" in grouped.columns:
            plt.plot(grouped.index, grouped["naive_r_err_causal"], '^:', label="Naive Receipt Baseline", color="#d62728")
        if "lamport_r_err_causal" in grouped.columns:
            plt.plot(grouped.index, grouped["lamport_r_err_causal"], 'd-.', label="Lamport Scalar Clock", color="#9467bd")

        plt.xlabel("Mean Blackout Duration (Hours)")
        plt.ylabel("Causal Misordering Rate ($R_{err}$)")
        plt.title("Causal Inversion Rate vs Blackout Duration")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()
        plt.savefig(output_dir / "r_err_comparison.png", dpi=300)
        plt.close()

        # Plot 2: F_liab comparison
        plt.figure(figsize=(8, 5))
        if "vc_f_liab" in grouped.columns:
            plt.plot(grouped.index, grouped["vc_f_liab"], 'o-', label="Vector Clock (Zero Penalty)", color="#2ca02c", linewidth=2.5)
        if "fair_f_liab" in grouped.columns:
            plt.plot(grouped.index, grouped["fair_f_liab"], 's--', label="Fair Physical Baseline", color="#1f77b4")
        if "naive_f_liab" in grouped.columns:
            plt.plot(grouped.index, grouped["naive_f_liab"], '^:', label="Naive Receipt Baseline", color="#d62728")

        plt.xlabel("Mean Blackout Duration (Hours)")
        plt.ylabel("False Liability Attribution Rate ($F_{liab}$)")
        plt.title("False Custody Attribution vs Network Blackouts")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()
        plt.savefig(output_dir / "f_liab_comparison.png", dpi=300)
        plt.close()

        # Plot 3: S_prune storage consolidation
        if "s_prune" in grouped.columns:
            plt.figure(figsize=(8, 5))
            plt.plot(grouped.index, grouped["s_prune"] * 100.0, 'o-', color="#ff7f0e", linewidth=2.5)
            plt.xlabel("Mean Blackout Duration (Hours)")
            plt.ylabel("State Storage Reduction ($S_{prune}$ %)")
            plt.title("Matrix-Clock Garbage Collection Efficiency")
            plt.ylim(0, 105)
            plt.grid(True, linestyle="--", alpha=0.6)
            plt.tight_layout()
            plt.savefig(output_dir / "s_prune_efficiency.png", dpi=300)
            plt.close()

    print(f"[Success] Generated benchmark charts in {output_dir}")

def main():
    parser = argparse.ArgumentParser(description="Plot cold chain experiment results.")
    parser.add_argument("--input", default="data/sweeps/results.csv", help="Input CSV path (default: data/sweeps/results.csv)")
    parser.add_argument("--output-dir", default="data/sweeps", help="Output directory")
    args = parser.parse_args()

    plot_sweeps(Path(args.input), Path(args.output_dir))

if __name__ == "__main__":
    main()

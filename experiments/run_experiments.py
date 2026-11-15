#!/usr/bin/env python3
"""
run_experiments.py — Cross-platform experiment runner and statistical analyzer.
Works on Windows, Linux, and macOS.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Run cold-chain experiments and sweeps.")
    parser.add_argument("--bin-dir", default="build", help="Path to build directory containing sweep_runner")
    parser.add_argument("--out-dir", default="data/sweeps", help="Directory for output CSVs and plots")
    parser.add_argument("--runs-per-point", type=int, default=30, help="Repetitions per parameter point")
    parser.add_argument("--mode", default="blackout", choices=["blackout", "drift", "ablation"], help="Sweep mode")
    parser.add_argument("--shipments", type=int, default=5, help="Number of concurrent shipments")
    parser.add_argument("--duration-hours", type=float, default=24.0, help="Simulation duration")
    args = parser.parse_args()

    bin_dir = Path(args.bin_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    exe_name = "sweep_runner.exe" if sys.platform.startswith("win") else "sweep_runner"
    sweep_bin = bin_dir / exe_name

    if not sweep_bin.exists():
        print(f"ERROR: {sweep_bin} not found. Please build the project first.")
        sys.exit(1)

    csv_file = out_dir / f"{args.mode}_sweep.csv"
    cmd = [
        str(sweep_bin),
        "--mode", args.mode,
        "--runs-per-point", str(args.runs_per_point),
        "--shipments", str(args.shipments),
        "--duration-hours", str(args.duration_hours),
        "--out", str(csv_file)
    ]

    print("=" * 60)
    print(f" Running {args.mode} sweep with {args.runs_per_point} runs per point...")
    print(f" Command: {' '.join(cmd)}")
    print("=" * 60)

    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"ERROR: Experiment run failed with code {res.returncode}")
        sys.exit(res.returncode)

    print(f"\n[Success] Experiment finished. CSV written to: {csv_file}")

if __name__ == "__main__":
    main()

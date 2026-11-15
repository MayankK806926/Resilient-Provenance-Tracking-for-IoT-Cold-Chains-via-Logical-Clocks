#!/usr/bin/env python3
"""
plot_results.py — Evaluation Plotter for Project Proposal (§4.1)

Generates the three promised empirical evaluation plots:
  1. Causal Misordering Rate (R_err) vs. Average Offline Duration (§4.1)
  2. False Liability Attribution Rate (F_liab) vs. Average Offline Duration (§4.1)
  3. State Consolidation & Autonomous Log Pruning (S_prune) vs. Offline Duration (§2.3, §3.2)
"""

import argparse
import os
import sys

import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np


def load_data(csv_path: str) -> pd.DataFrame:
    if not os.path.exists(csv_path):
        print(f"ERROR: CSV not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    df = pd.read_csv(csv_path)
    required = ['blackout_hours', 'r_err', 'f_liab', 's_prune',
                'unpruned_store', 'pruned_store']
    missing = [c for c in required if c not in df.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}", file=sys.stderr)
        sys.exit(1)

    return df


def plot_rerr(df: pd.DataFrame, output_dir: str):
    """Plot R_err vs average offline duration (§4.1)."""
    grouped = df.groupby('blackout_hours').agg(
        naive_rerr_mean=('r_err', 'mean'),
        naive_rerr_std=('r_err', 'std'),
    ).reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.errorbar(grouped['blackout_hours'], grouped['naive_rerr_mean'],
                yerr=grouped['naive_rerr_std'],
                fmt='o-', color='#e74c3c', linewidth=2, markersize=8,
                capsize=5, capthick=1.5, elinewidth=1.5,
                label='Naive Physical-Timestamp Baseline (with drift & jitter)',
                zorder=3)

    ax.axhline(y=0, color='#2ecc71', linewidth=2.5, linestyle='--',
               label='Causal-Order Pipeline (guaranteed zero violations)', zorder=2)

    ax.set_xlabel('Average Offline Duration (hours)', fontsize=13)
    ax.set_ylabel('Causal Misordering Rate ($R_{err}$)', fontsize=13)
    ax.set_title('Causal Misordering Rate ($R_{err}$) vs. Average Offline Duration', fontsize=14, fontweight='bold')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(fontsize=11, loc='upper left')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=-0.01)

    fig.tight_layout()
    path = os.path.join(output_dir, 'rerr_vs_blackout.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def plot_fliab(df: pd.DataFrame, output_dir: str):
    """Plot F_liab vs average offline duration (§4.1)."""
    grouped = df.groupby('blackout_hours').agg(
        fliab_mean=('f_liab', 'mean'),
        fliab_std=('f_liab', 'std'),
    ).reset_index()

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.errorbar(grouped['blackout_hours'], grouped['fliab_mean'],
                yerr=grouped['fliab_std'],
                fmt='s-', color='#e67e22', linewidth=2, markersize=8,
                capsize=5, capthick=1.5, elinewidth=1.5,
                label='Naive Physical-Timestamp Baseline (misattribution due to drift)',
                zorder=3)

    ax.axhline(y=0, color='#2ecc71', linewidth=2.5, linestyle='--',
               label='Causal-Order Pipeline (zero false liability attribution)', zorder=2)

    ax.set_xlabel('Average Offline Duration (hours)', fontsize=13)
    ax.set_ylabel('False Liability Attribution Rate ($F_{liab}$)', fontsize=13)
    ax.set_title('False Liability Attribution Rate ($F_{liab}$) vs. Offline Duration',
                 fontsize=14, fontweight='bold')
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(fontsize=11, loc='upper left')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=-0.01)

    fig.tight_layout()
    path = os.path.join(output_dir, 'fliab_vs_blackout.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def plot_storage(df: pd.DataFrame, output_dir: str):
    """Plot State Consolidation / Log Pruning (S_prune) vs offline duration (§2.3, §3.2)."""
    grouped = df.groupby('blackout_hours').agg(
        sprune_mean=('s_prune', 'mean'),
        sprune_std=('s_prune', 'std'),
        unpruned_mean=('unpruned_store', 'mean'),
        pruned_mean=('pruned_store', 'mean'),
    ).reset_index()

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    ax1.errorbar(grouped['blackout_hours'], grouped['sprune_mean'] * 100,
                 yerr=grouped['sprune_std'] * 100,
                 fmt='D-', color='#3498db', linewidth=2, markersize=8,
                 capsize=5, capthick=1.5, elinewidth=1.5,
                 label='State Consolidation Ratio ($S_{prune}$)', zorder=3)
    ax1.set_xlabel('Average Offline Duration (hours)', fontsize=13)
    ax1.set_ylabel('Storage Reduction Ratio ($S_{prune}$) %', fontsize=13)
    ax1.set_title('Matrix-Clock Autonomous Log-Pruning Efficiency', fontsize=14, fontweight='bold')
    ax1.set_xscale('log')
    ax1.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax1.legend(fontsize=11)
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim(bottom=-5, top=105)

    x = np.arange(len(grouped))
    width = 0.35
    ax2.bar(x - width/2, grouped['unpruned_mean'], width,
            color='#e74c3c', alpha=0.8, label='Unbounded Active Store (No GC)')
    ax2.bar(x + width/2, grouped['pruned_mean'], width,
            color='#2ecc71', alpha=0.8, label='Pruned Active Store (Matrix-Clock GC)')
    ax2.set_xlabel('Average Offline Duration (hours)', fontsize=13)
    ax2.set_ylabel('Active Memory Records', fontsize=13)
    ax2.set_title('State Consolidation: Active Records in Memory', fontsize=14, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels([f'{h:.1f}' for h in grouped['blackout_hours']])
    ax2.legend(fontsize=11)
    ax2.grid(True, alpha=0.3, axis='y')

    fig.tight_layout()
    path = os.path.join(output_dir, 'storage_reduction.png')
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  Saved: {path}")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description='Generate evaluation figures promised in project proposal (§4.1).')
    parser.add_argument('--input', default='data/sweeps/results.csv',
                        help='Path to sweep CSV (default: data/sweeps/results.csv)')
    parser.add_argument('--output-dir', default='data/sweeps/',
                        help='Directory for output PNGs (default: data/sweeps/)')
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    df = load_data(args.input)

    plot_rerr(df, args.output_dir)
    plot_fliab(df, args.output_dir)
    plot_storage(df, args.output_dir)

    print("All evaluation plots generated.")


if __name__ == '__main__':
    main()

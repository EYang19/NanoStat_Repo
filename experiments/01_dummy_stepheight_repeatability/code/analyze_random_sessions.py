#!/usr/bin/env python3
"""Analyze the two randomized NanoStat dummy-cell sessions."""

import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
EXPECTED = 70.187


def read_summary(path):
    rows = []
    in_summary = False
    with path.open(newline="") as handle:
        for line in handle:
            if line.strip() == "SUMMARY":
                in_summary = True
                header = next(handle).strip().split(",")
                continue
            if in_summary and line.startswith("RAW_DATA"):
                break
            if in_summary and line.strip():
                row = dict(zip(header, line.strip().split(",")))
                for key in ("dummy_mean_delta_nA", "dummy_error_pct", "dummy_scan_ms"):
                    row[key] = float(row[key])
                rows.append(row)
    return rows


def main():
    FIGURES.mkdir(exist_ok=True)
    files = sorted(DATA.glob("dummyrandomsession*.csv"))
    rows = [row for path in files for row in read_summary(path)]
    profiles = ["5MV", "2MV", "1MVSEG"]
    labels = {"5MV": "5.37 mV full", "2MV": "2.15 mV full", "1MVSEG": "1.07 mV segmented"}

    summary = []
    for profile in profiles:
        values = np.array([r["dummy_mean_delta_nA"] for r in rows if r["dummy_profile"] == profile])
        errors = np.array([r["dummy_error_pct"] for r in rows if r["dummy_profile"] == profile])
        scan_ms = rows[[r["dummy_profile"] for r in rows].index(profile)]["dummy_scan_ms"]
        mean = float(values.mean())
        sd = float(values.std(ddof=1))
        summary.append((labels[profile], len(values), mean, sd, 100 * sd / mean, float(errors.mean()), scan_ms))

    with (ROOT / "random_session_summary.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["profile", "n", "mean_delta_nA", "run_to_run_sd_nA", "cv_pct", "mean_error_pct", "scan_ms"])
        writer.writerows(summary)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8), constrained_layout=True)
    for profile, label in zip(profiles, [x[0] for x in summary]):
        values = np.array([r["dummy_mean_delta_nA"] for r in rows if r["dummy_profile"] == profile])
        axes[0].plot(np.arange(1, len(values) + 1), values, "o-", label=label)
    axes[0].axhline(EXPECTED, color="black", linestyle="--", linewidth=1, label="Expected 70.187 nA")
    axes[0].set(xlabel="Run number across two sessions", ylabel="Mean differential current (nA)", title="Dummy-cell repeatability")
    axes[0].grid(alpha=0.25)
    axes[0].legend(fontsize=8)

    x = np.arange(len(summary))
    means = [item[2] for item in summary]
    sds = [item[3] for item in summary]
    axes[1].errorbar(x, means, yerr=sds, fmt="o", capsize=4, color="#1565c0")
    axes[1].axhline(EXPECTED, color="black", linestyle="--", linewidth=1)
    axes[1].set_xticks(x, [item[0] for item in summary], rotation=20, ha="right")
    axes[1].set(ylabel="Mean differential current (nA)", title="Mean and run-to-run SD")
    axes[1].grid(alpha=0.25)
    fig.savefig(FIGURES / "dummy_stepheight_repeatability.png", dpi=220)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7.2, 4.5), constrained_layout=True)
    ax.bar(x, [item[4] for item in summary], color=["#5b8ff9", "#61dDAA", "#65789b"])
    ax.set_xticks(x, [item[0] for item in summary], rotation=20, ha="right")
    ax.set_ylabel("Run-to-run CV (%)")
    ax.set_title("Repeatability versus SWV step height")
    ax.grid(axis="y", alpha=0.25)
    fig.savefig(FIGURES / "dummy_stepheight_cv.png", dpi=220)
    plt.close(fig)


if __name__ == "__main__":
    main()

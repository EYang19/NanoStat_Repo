#!/usr/bin/env python3
"""Compare dummy-cell noise under four NanoStat power conditions."""

import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
EXPECTED_NA = 70.187

CONDITIONS = {
    "Battery only": ["5mVBattery.csv", "5mVBattery_run2.csv"],
    "USB-C + battery, MacBook unplugged": ["5mVUSBC.csv", "5mVUSBC_run2.csv"],
    "USB-C + battery, MacBook on mains": ["5mVUSBC_MainsOn.csv"],
    "USB-C only, no battery": ["5mVUSBC_only.csv", "5mVUSBC_only_run2.csv"],
}


def parse_file(path):
    summary = []
    raw = []
    in_summary = False
    in_raw = False
    summary_header = None
    raw_header = None
    with path.open(newline="") as handle:
        for line in handle:
            line = line.strip()
            if line == "SUMMARY":
                in_summary = True
                summary_header = next(handle).strip().split(",")
                continue
            if line == "RAW_DATA":
                in_summary = False
                in_raw = True
                raw_header = next(handle).strip().split(",")
                continue
            if in_summary and line:
                row = dict(zip(summary_header, line.split(",")))
                for key in ("run_id", "dummy_mean_delta_nA", "dummy_sd_delta_nA", "dummy_expected_delta_nA", "dummy_error_pct", "dummy_scan_ms"):
                    row[key] = float(row[key])
                summary.append(row)
            elif in_raw and line:
                row = dict(zip(raw_header, line.split(",")))
                row["run_id"] = int(float(row["run_id"]))
                for key in ("E_WE_RE_mV", "Delta_I_nA"):
                    row[key] = float(row[key])
                raw.append(row)
    return summary, raw


def rms(values):
    values = np.asarray(values, dtype=float)
    return float(np.sqrt(np.mean((values - values.mean()) ** 2)))


def main():
    FIGURES.mkdir(exist_ok=True)
    all_summary = []
    all_raw = []
    condition_order = []

    for condition, filenames in CONDITIONS.items():
        for session_index, filename in enumerate(filenames, start=1):
            summary, raw = parse_file(DATA / filename)
            session = Path(filename).stem
            for row in summary:
                row["condition"] = condition
                row["session"] = session
                all_summary.append(row)
            for row in raw:
                row["condition"] = condition
                row["session"] = session
                all_raw.append(row)
        condition_order.append(condition)

    metrics = []
    traces = {}
    for condition in condition_order:
        condition_summary = [r for r in all_summary if r["condition"] == condition]
        condition_raw = [r for r in all_raw if r["condition"] == condition]
        traces[condition] = []
        run_keys = sorted({(r["session"], r["run_id"]) for r in condition_raw})
        for session, run_id in run_keys:
            points = [r for r in condition_raw if r["session"] == session and r["run_id"] == run_id]
            points.sort(key=lambda r: r["E_WE_RE_mV"])
            current = np.array([r["Delta_I_nA"] for r in points])
            potential = np.array([r["E_WE_RE_mV"] for r in points])
            baseline = current[(potential >= -400) & (potential <= -330)]
            fixed = current[np.argmin(np.abs(potential - (-300.0)))]
            summary_row = next(r for r in condition_summary if r["session"] == session and int(r["run_id"]) == run_id)
            metrics.append({
                "condition": condition,
                "session": summary_row["session"],
                "run_id": run_id,
                "mean_delta_nA": summary_row["dummy_mean_delta_nA"],
                "within_scan_sd_nA": summary_row["dummy_sd_delta_nA"],
                "baseline_rms_nA": rms(baseline),
                "fixed_minus300_nA": fixed,
                "error_pct": summary_row["dummy_error_pct"],
                "scan_ms": summary_row["dummy_scan_ms"],
            })
            traces[condition].append((potential, current))

    with (DATA / "per_run_power_noise_metrics.csv").open("w", newline="") as handle:
        fields = list(metrics[0].keys())
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(metrics)

    summaries = []
    for condition in condition_order:
        rows = [r for r in metrics if r["condition"] == condition]
        def mean(key):
            return float(np.mean([r[key] for r in rows]))
        def sd(key):
            return float(np.std([r[key] for r in rows], ddof=1))
        summaries.append({
            "condition": condition,
            "n": len(rows),
            "mean_delta_nA": mean("mean_delta_nA"),
            "run_to_run_sd_nA": sd("mean_delta_nA"),
            "run_to_run_cv_pct": 100 * sd("mean_delta_nA") / mean("mean_delta_nA"),
            "within_scan_sd_mean_nA": mean("within_scan_sd_nA"),
            "baseline_rms_mean_nA": mean("baseline_rms_nA"),
            "fixed_minus300_sd_nA": sd("fixed_minus300_nA"),
            "mean_error_pct": mean("error_pct"),
        })

    with (DATA / "power_condition_summary.csv").open("w", newline="") as handle:
        fields = list(summaries[0].keys())
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summaries)

    # Comparison of the four main noise metrics.
    metric_specs = [
        ("run_to_run_sd_nA", "Run-to-run SD (nA)"),
        ("within_scan_sd_mean_nA", "Within-scan SD (nA)"),
        ("baseline_rms_mean_nA", "Baseline RMS (nA)"),
        ("fixed_minus300_sd_nA", "Fixed-potential SD (nA)"),
    ]
    fig, axes = plt.subplots(2, 2, figsize=(13, 8), constrained_layout=True)
    labels = [s["condition"] for s in summaries]
    for ax, (key, ylabel) in zip(axes.flat, metric_specs):
        values = [s[key] for s in summaries]
        bars = ax.bar(np.arange(len(labels)), values, color=["#2f5597", "#70ad47", "#ed7d31", "#a64d79"])
        ax.set_xticks(np.arange(len(labels)), labels, rotation=25, ha="right")
        ax.set_ylabel(ylabel)
        ax.grid(axis="y", alpha=0.25)
        for bar, value in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), f"{value:.3f}", ha="center", va="bottom", fontsize=8)
    fig.suptitle("Dummy-cell noise under four power conditions")
    fig.savefig(FIGURES / "usb_battery_noise_comparison.png", dpi=220)
    plt.close(fig)

    # Overlay only condition means so that power-dependent curve shape remains
    # readable without the individual-run traces obscuring the comparison.
    fig, ax = plt.subplots(figsize=(10, 6), constrained_layout=True)
    colors = ["#2f5597", "#70ad47", "#ed7d31", "#a64d79"]
    for condition, color in zip(condition_order, colors):
        grid = traces[condition][0][0]
        matrix = np.array([trace[1] for trace in traces[condition]])
        ax.plot(grid, matrix.mean(axis=0), color=color, linewidth=2.2, label=condition)
    ax.set(xlabel="Potential E_WE-RE (mV)", ylabel="Delta I (nA)", title="Dummy-cell SWV traces: all runs and condition means")
    ax.grid(alpha=0.25)
    ax.legend(fontsize=8)
    fig.savefig(FIGURES / "usb_battery_trace_overlay.png", dpi=220)
    plt.close(fig)

    # Run means and expected current.
    fig, ax = plt.subplots(figsize=(11, 5.5), constrained_layout=True)
    for index, (condition, color) in enumerate(zip(condition_order, colors)):
        values = [r["mean_delta_nA"] for r in metrics if r["condition"] == condition]
        x = np.full(len(values), index, dtype=float) + np.linspace(-0.16, 0.16, len(values))
        ax.scatter(x, values, color=color, alpha=0.8, s=30, label=condition)
    ax.axhline(EXPECTED_NA, color="black", linestyle="--", linewidth=1, label="Expected 70.187 nA")
    ax.set_xticks(np.arange(len(labels)), labels, rotation=25, ha="right")
    ax.set_ylabel("Mean differential current (nA)")
    ax.set_title("Run-to-run mean current")
    ax.grid(axis="y", alpha=0.25)
    ax.legend(fontsize=8)
    fig.savefig(FIGURES / "usb_battery_run_means.png", dpi=220)
    plt.close(fig)


if __name__ == "__main__":
    main()

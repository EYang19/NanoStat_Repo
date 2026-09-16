#!/usr/bin/env python3
"""Analyse and plot the NI myDAQ 5 mV voltage-profile capture."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
INPUT = ROOT / "data" / "5mV.csv"
OUTPUT = ROOT / "figures" / "mydaq_5mV_voltage_profile.png"


def read_capture(path: Path) -> tuple[np.ndarray, np.ndarray]:
    times: list[float] = []
    volts: list[float] = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.reader(handle):
            if len(row) < 2:
                continue
            try:
                times.append(float(row[0]))
                volts.append(float(row[1]))
            except ValueError:
                continue
    return np.asarray(times), np.asarray(volts) * 1000.0


def binned_median(time_s: np.ndarray, voltage_mv: np.ndarray, width_s: float) -> tuple[np.ndarray, np.ndarray]:
    edges = np.arange(time_s[0], time_s[-1] + width_s, width_s)
    indices = np.digitize(time_s, edges) - 1
    x: list[float] = []
    y: list[float] = []
    for index in range(len(edges) - 1):
        values = voltage_mv[indices == index]
        if values.size:
            x.append((edges[index] + edges[index + 1]) / 2.0)
            y.append(float(np.median(values)))
    return np.asarray(x), np.asarray(y)


def main() -> None:
    time_s, voltage_mv = read_capture(INPUT)
    if not len(time_s):
        raise SystemExit("No numeric samples found")

    x_bin, y_bin = binned_median(time_s, voltage_mv, 0.005)
    scan_start = 3.85
    period_s = 1.0 / 120.0
    cycle_indices = np.arange(85)
    low_levels = np.asarray(
        [
            np.median(
                voltage_mv[
                    (time_s >= scan_start + i * period_s + 0.006)
                    & (time_s < scan_start + i * period_s + 0.008)
                ]
            )
            for i in cycle_indices
        ]
    )
    high_levels = np.asarray(
        [
            np.median(
                voltage_mv[
                    (time_s >= scan_start + i * period_s + 0.002)
                    & (time_s < scan_start + i * period_s + 0.005)
                ]
            )
            for i in cycle_indices
        ]
    )
    step_height = float(np.median(np.diff(low_levels)))
    # The firmware changes the staircase level between the pulse-high and
    # pulse-low halves. Compare high_i with low_(i-1) to recover the pulse
    # peak-to-peak amplitude without subtracting one step height.
    pulse_pp = float(np.median(high_levels[1:] - low_levels[:-1]))
    plt.rcParams.update({"font.size": 11, "axes.labelsize": 12, "axes.titlesize": 14})
    fig, (ax_full, ax_edges) = plt.subplots(2, 1, figsize=(11, 7), gridspec_kw={"height_ratios": [2, 1]})

    ax_full.plot(time_s, voltage_mv, color="#aab4bf", linewidth=0.25, alpha=0.25, label="Raw myDAQ samples")
    ax_full.plot(x_bin, y_bin, color="#1f77b4", linewidth=1.8, label="5 ms median")
    ax_full.axhline(-450.0, color="#d62728", linestyle="--", linewidth=1.0, label="-450 mV reference")
    ax_full.axhline(0.0, color="#2ca02c", linestyle="--", linewidth=1.0, label="0 mV reference")
    ax_full.set_title("NI myDAQ capture: NanoStat 5 mV SWV voltage profile")
    ax_full.set_ylabel("Measured V_WE-RE (mV)")
    ax_full.grid(True, color="#d9e0e8", linewidth=0.7)
    ax_full.legend(loc="lower right", ncol=2, framealpha=0.9)

    detail_end = scan_start + 12 * period_s
    detail_mask = (time_s >= scan_start) & (time_s < detail_end)
    ax_edges.plot(time_s[detail_mask], voltage_mv[detail_mask], color="#aab4bf", linewidth=0.45, alpha=0.45)
    detail_cycles = cycle_indices[:12]
    cycle_times = scan_start + detail_cycles * period_s
    ax_edges.plot(cycle_times + 0.003, high_levels[:12], "o-", color="#d62728", markersize=3, linewidth=1.2, label="Pulse-high level")
    ax_edges.plot(cycle_times + 0.007, low_levels[:12], "o-", color="#1f77b4", markersize=3, linewidth=1.2, label="Pulse-low level")
    ax_edges.set_xlim(scan_start, detail_end)
    ax_edges.set_ylim(-500, -370)
    ax_edges.set_xlabel("Time from myDAQ capture start (s)")
    ax_edges.set_ylabel("mV")
    ax_edges.grid(True, color="#d9e0e8", linewidth=0.7)
    ax_edges.set_title(
        f"12-cycle detail: {step_height:.3f} mV staircase, "
        f"{pulse_pp:.1f} mV reconstructed pulse peak-to-peak"
    )
    ax_edges.legend(loc="lower right", fontsize=9, framealpha=0.9)

    fig.text(
        0.02,
        0.01,
        f"N={len(time_s):,}; duration={time_s[-1] - time_s[0]:.5f} s; "
        f"nominal interval={np.median(np.diff(time_s)) * 1e6:.1f} us; "
        f"range={voltage_mv.min():.1f} to {voltage_mv.max():.1f} mV; "
        f"cycle period={period_s * 1000:.3f} ms",
        color="#344054",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUTPUT, dpi=250)
    print(f"samples={len(time_s)}")
    print(f"duration_s={time_s[-1] - time_s[0]:.5f}")
    print(f"sample_interval_us={np.median(np.diff(time_s)) * 1e6:.2f}")
    print(f"voltage_range_mV={voltage_mv.min():.3f},{voltage_mv.max():.3f}")
    print(OUTPUT)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Plot the updated 2 mV and segmented 1 mV myDAQ captures."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
OUT = ROOT / "figures" / "mydaq_2mV_1mVseg_profiles.png"


def read_capture(path: Path) -> tuple[np.ndarray, np.ndarray]:
    t: list[float] = []
    v: list[float] = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.reader(handle):
            if len(row) < 2:
                continue
            try:
                t.append(float(row[0]))
                v.append(float(row[1]) * 1000.0)
            except ValueError:
                continue
    return np.asarray(t), np.asarray(v)


def median_bins(t: np.ndarray, v: np.ndarray, width_s: float = 0.005) -> tuple[np.ndarray, np.ndarray]:
    edges = np.arange(t[0], t[-1] + width_s, width_s)
    idx = np.digitize(t, edges) - 1
    x: list[float] = []
    y: list[float] = []
    for i in range(len(edges) - 1):
        z = v[idx == i]
        if len(z):
            x.append((edges[i] + edges[i + 1]) / 2)
            y.append(float(np.median(z)))
    return np.asarray(x), np.asarray(y)


def main() -> None:
    captures = [("2 mV full sequencer", DATA / "2mV.csv"), ("1 mV segmented", DATA / "1mVseg.csv")]
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharey=True)
    for ax, (label, path) in zip(axes, captures):
        t, v = read_capture(path)
        x, y = median_bins(t, v)
        ax.plot(t, v, color="#aab4bf", linewidth=0.2, alpha=0.16, label="Raw myDAQ samples")
        ax.plot(x, y, color="#1f77b4", linewidth=1.3, label="5 ms median")
        ax.axhline(-450, color="#d62728", linestyle="--", linewidth=0.8)
        ax.axhline(0, color="#2ca02c", linestyle="--", linewidth=0.8)
        ax.set_title(f"{label}: {t[-1] - t[0]:.3f} s capture, {len(t):,} samples")
        ax.set_ylabel("V_WE-RE (mV)")
        ax.grid(True, color="#d9e0e8", linewidth=0.6)
        ax.legend(loc="lower right", fontsize=9, framealpha=0.9)
    axes[-1].set_xlabel("Time from myDAQ capture start (s)")
    fig.suptitle("Updated NanoStat SWV voltage captures", fontsize=16)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(OUT, dpi=250)
    print(OUT)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Create a schematic kinetic differential measurement (KDM) figure."""

from __future__ import annotations

import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/private/tmp/matplotlib-nanostat")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "figures" / "kdm_concept.png"

BLUE = "#1769aa"
ORANGE = "#d97706"
GREEN = "#238636"
PURPLE = "#7a4ca5"
GREY = "#56616f"
GRID = "#d7dde5"
TARGET = "#d9e2ec"


def smooth_pulse(time: np.ndarray, start: float, end: float, edge: float = 1.8) -> np.ndarray:
    rise = 1.0 / (1.0 + np.exp(-(time - start) / edge))
    fall = 1.0 / (1.0 + np.exp((time - end) / edge))
    return rise * fall


def main() -> None:
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 10.2,
            "axes.titlesize": 11.2,
            "axes.labelsize": 10.2,
            "xtick.labelsize": 9.1,
            "ytick.labelsize": 9.1,
            "legend.fontsize": 8.8,
            "axes.linewidth": 0.9,
        }
    )

    fig = plt.figure(figsize=(8.1, 5.75))
    grid = fig.add_gridspec(2, 2, height_ratios=(1.0, 1.02), hspace=0.42, wspace=0.28)
    ax_kinetic = fig.add_subplot(grid[0, :])
    ax_raw = fig.add_subplot(grid[1, 0])
    ax_kdm = fig.add_subplot(grid[1, 1])

    # (a) Conceptual frequency dependence of the bound and unbound probe.
    frequency = np.linspace(0.04, 0.96, 300)
    unbound = 1.13 - 0.24 * frequency + 0.015 * np.cos(np.pi * frequency)
    bound = 0.66 + 0.73 * frequency - 0.02 * np.cos(np.pi * frequency)
    ax_kinetic.plot(frequency, unbound, color=GREY, linewidth=2.2, label="Unbound probe")
    ax_kinetic.plot(frequency, bound, color=PURPLE, linewidth=2.4, label="Target-bound probe")

    f_off = 0.22
    f_on = 0.78
    for x_pos, color in ((f_off, ORANGE), (f_on, BLUE)):
        ax_kinetic.axvline(x_pos, color=color, linewidth=1.2, linestyle=(0, (4, 3)))

    unbound_off = np.interp(f_off, frequency, unbound)
    bound_off = np.interp(f_off, frequency, bound)
    unbound_on = np.interp(f_on, frequency, unbound)
    bound_on = np.interp(f_on, frequency, bound)
    ax_kinetic.annotate(
        "",
        xy=(f_off, bound_off),
        xytext=(f_off, unbound_off),
        arrowprops={"arrowstyle": "->", "color": ORANGE, "linewidth": 2.0},
    )
    ax_kinetic.annotate(
        "",
        xy=(f_on, bound_on),
        xytext=(f_on, unbound_on),
        arrowprops={"arrowstyle": "->", "color": BLUE, "linewidth": 2.0},
    )
    ax_kinetic.text(f_off - 0.025, 0.71, "Signal-off\nresponse", color=ORANGE, ha="right", va="center", fontweight="bold")
    ax_kinetic.text(f_on + 0.025, 1.28, "Signal-on\nresponse", color=BLUE, ha="left", va="center", fontweight="bold")
    ax_kinetic.text(f_off, 0.55, r"lower $f_{\mathrm{off}}$", color=ORANGE, ha="center", va="top")
    ax_kinetic.text(f_on, 0.55, r"higher $f_{\mathrm{on}}$", color=BLUE, ha="center", va="top")
    ax_kinetic.set_xlim(0.0, 1.0)
    ax_kinetic.set_ylim(0.52, 1.43)
    ax_kinetic.set_xticks([])
    ax_kinetic.set_yticks([])
    ax_kinetic.set_xlabel("Increasing SWV interrogation frequency")
    ax_kinetic.set_ylabel("Peak current\n(conceptual)")
    ax_kinetic.set_title("(a) Frequency-dependent signal inversion", loc="left", fontweight="bold")
    ax_kinetic.legend(loc="upper center", ncol=2, frameon=True, framealpha=0.96)

    # (b) Matched common-mode drift with opposite target responses.
    time = np.linspace(0.0, 100.0, 700)
    target = smooth_pulse(time, 34.0, 68.0)
    common_drift = 1.0 - 0.00125 * time + 0.004 * np.sin(2.0 * np.pi * time / 45.0)
    current_on = common_drift * (1.0 + 0.17 * target)
    current_off = common_drift * (1.0 - 0.12 * target)
    kdm = (current_on - current_off) / ((current_on + current_off) / 2.0)

    ax_raw.axvspan(34.0, 68.0, color=TARGET, alpha=0.72, linewidth=0)
    ax_raw.plot(time, current_on, color=BLUE, linewidth=2.1, label=r"$I_{\mathrm{on}}$")
    ax_raw.plot(time, current_off, color=ORANGE, linewidth=2.1, label=r"$I_{\mathrm{off}}$")
    ax_raw.plot(time, common_drift, color=GREY, linewidth=1.1, linestyle=(0, (3, 3)), label="Shared drift")
    ax_raw.text(51.0, 1.19, "Target present", color="#344054", ha="center", va="center")
    ax_raw.annotate(
        "Common-mode drift",
        xy=(90.0, np.interp(90.0, time, common_drift)),
        xytext=(54.0, 0.84),
        color=GREY,
        arrowprops={"arrowstyle": "->", "color": GREY, "linewidth": 0.9},
    )
    ax_raw.set_xlim(0.0, 100.0)
    ax_raw.set_ylim(0.80, 1.22)
    ax_raw.set_xlabel("Time")
    ax_raw.set_ylabel("Normalised peak current")
    ax_raw.set_title("(b) Paired peak-current traces", loc="left", fontweight="bold")
    ax_raw.grid(color=GRID, linewidth=0.75)
    ax_raw.legend(loc="lower left", frameon=True, framealpha=0.96, ncol=1)

    # (c) Normalised difference suppresses the common component.
    ax_kdm.axvspan(34.0, 68.0, color=TARGET, alpha=0.72, linewidth=0)
    ax_kdm.axhline(0.0, color=GREY, linewidth=1.0, linestyle=(0, (3, 3)))
    ax_kdm.plot(time, kdm, color=GREEN, linewidth=2.6)
    ax_kdm.text(51.0, 0.305, "Target present", color="#344054", ha="center", va="center")
    ax_kdm.text(
        50.0,
        0.17,
        r"$\mathrm{KDM}=(I_{\mathrm{on}}-I_{\mathrm{off}})/\bar I$"
        "\n"
        r"$\bar I=(I_{\mathrm{on}}+I_{\mathrm{off}})/2$",
        ha="center",
        va="center",
        color="#123f20",
        bbox={"boxstyle": "round,pad=0.32", "facecolor": "white", "edgecolor": "#b9c8bc", "alpha": 0.96},
    )
    ax_kdm.annotate(
        "Common drift suppressed",
        xy=(88.0, np.interp(88.0, time, kdm)),
        xytext=(69.0, 0.055),
        ha="center",
        color=GREEN,
        arrowprops={"arrowstyle": "->", "color": GREEN, "linewidth": 0.9},
    )
    ax_kdm.set_xlim(0.0, 100.0)
    ax_kdm.set_ylim(-0.025, 0.335)
    ax_kdm.set_xlabel("Time")
    ax_kdm.set_ylabel("KDM response")
    ax_kdm.set_title("(c) KDM response", loc="left", fontweight="bold")
    ax_kdm.grid(color=GRID, linewidth=0.75)

    for axis in (ax_kinetic, ax_raw, ax_kdm):
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.subplots_adjust(left=0.105, right=0.985, top=0.945, bottom=0.095)
    fig.savefig(OUT, dpi=300, facecolor="white")
    print(OUT)


if __name__ == "__main__":
    main()

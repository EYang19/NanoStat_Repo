#!/usr/bin/env python3
"""Draw the NanoStat 120 Hz SWV waveform and endpoint extraction timing."""

from __future__ import annotations

import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/private/tmp/matplotlib-nanostat")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "figures" / "swv_waveform_endpoint_sampling.png"

# Active NanoStat 5 mV profile settings from src/ad5941_app.c.
FREQUENCY_HZ = 120.0
PERIOD_MS = 1000.0 / FREQUENCY_HZ
HALF_PERIOD_MS = PERIOD_MS / 2.0
LPDAC_LSB_MV = 0.5372
STEP_CODE = 10
PULSE_CODE = 65
STEP_MV = STEP_CODE * LPDAC_LSB_MV
PULSE_MV = PULSE_CODE * LPDAC_LSB_MV
START_BASE_MV = -(0xB46 - 0x800) * LPDAC_LSB_MV

ENDPOINT_PCT = 0.05
GUARD_PCT = 0.02
SAMPLES_PER_HALF = 30


def potential_trace(step_count: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Return a step-drawn SWV trace and its staircase baseline."""
    time: list[float] = []
    potential: list[float] = []
    base_time: list[float] = []
    base_potential: list[float] = []

    for step in range(step_count):
        t0 = step * PERIOD_MS
        base = START_BASE_MV + step * STEP_MV
        forward = base - PULSE_MV
        reverse = base + PULSE_MV
        time.extend((t0, t0 + HALF_PERIOD_MS, t0 + HALF_PERIOD_MS, t0 + PERIOD_MS))
        potential.extend((forward, forward, reverse, reverse))
        base_time.extend((t0, t0 + PERIOD_MS))
        base_potential.extend((base, base))

    return (
        np.asarray(time),
        np.asarray(potential),
        np.asarray(base_time),
        np.asarray(base_potential),
    )


def current_response(time_ms: np.ndarray) -> np.ndarray:
    """Schematic settling current used only to explain endpoint selection."""
    forward_inf = 1.0
    reverse_inf = 0.35
    tau_ms = 0.62
    current = np.empty_like(time_ms)
    first = time_ms < HALF_PERIOD_MS
    current[first] = forward_inf + 0.82 * np.exp(-time_ms[first] / tau_ms)
    local = time_ms[~first] - HALF_PERIOD_MS
    current[~first] = reverse_inf - 0.72 * np.exp(-local / tau_ms)
    return current


def endpoint_bounds(half_start_ms: float) -> tuple[float, float, float]:
    guard = GUARD_PCT * HALF_PERIOD_MS
    usable_end = half_start_ms + HALF_PERIOD_MS - guard
    endpoint_width = ENDPOINT_PCT * (HALF_PERIOD_MS - guard)
    return usable_end - endpoint_width, usable_end, half_start_ms + HALF_PERIOD_MS


def main() -> None:
    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 10.5,
            "axes.titlesize": 11.5,
            "axes.labelsize": 10.5,
            "xtick.labelsize": 9.5,
            "ytick.labelsize": 9.5,
            "legend.fontsize": 9.2,
            "axes.linewidth": 0.9,
        }
    )

    fig, (ax_v, ax_i) = plt.subplots(
        2,
        1,
        figsize=(8.1, 6.25),
        gridspec_kw={"height_ratios": [1.0, 1.15], "hspace": 0.43},
    )

    # Panel (a): programmed potential over four complete stairs.
    t_v, e_v, t_base, e_base = potential_trace(4)
    ax_v.plot(t_v, e_v, color="#1769aa", linewidth=2.25, label=r"Applied $E_{\mathrm{WE-RE}}$")
    ax_v.plot(
        t_base,
        e_base,
        color="#4f5965",
        linewidth=1.5,
        linestyle=(0, (4, 3)),
        label="Staircase baseline",
    )
    ax_v.set_xlim(0.0, 4.0 * PERIOD_MS)
    ax_v.set_ylim(START_BASE_MV - PULSE_MV - 8.0, START_BASE_MV + 3.0 * STEP_MV + PULSE_MV + 8.0)
    ax_v.set_ylabel(r"$E_{\mathrm{WE-RE}}$ (mV)")
    ax_v.set_title("(a) NanoStat 120 Hz SWV excitation (5 mV profile)", loc="left", fontweight="bold")
    ax_v.grid(axis="y", color="#d7dde5", linewidth=0.8)
    ax_v.legend(loc="lower right", frameon=True, framealpha=0.96)

    # Frequency and amplitude annotations.
    period_y = START_BASE_MV + PULSE_MV + 5.0
    ax_v.annotate(
        "",
        xy=(0.2, START_BASE_MV + PULSE_MV + 5.0),
        xytext=(PERIOD_MS - 0.2, START_BASE_MV + PULSE_MV + 5.0),
        arrowprops={"arrowstyle": "<->", "color": "#344054", "linewidth": 1.0},
    )
    ax_v.text(PERIOD_MS / 2.0, period_y + 1.2, "one period = 8.33 ms", ha="center", va="bottom", color="#344054")
    ax_v.annotate(
        "",
        xy=(HALF_PERIOD_MS * 0.82, START_BASE_MV - PULSE_MV),
        xytext=(HALF_PERIOD_MS * 0.82, START_BASE_MV + PULSE_MV),
        arrowprops={"arrowstyle": "<->", "color": "#1769aa", "linewidth": 1.0},
    )
    ax_v.text(
        0.75,
        START_BASE_MV - 3.0,
        "69.84 mV p-p",
        ha="left",
        va="center",
        color="#1769aa",
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.82, "pad": 1.5},
    )
    ax_v.annotate(
        r"$\Delta E_s=5.372$ mV",
        xy=(2.02 * PERIOD_MS, START_BASE_MV + 2.0 * STEP_MV),
        xytext=(2.45 * PERIOD_MS, START_BASE_MV - 4.5),
        arrowprops={"arrowstyle": "->", "color": "#4f5965", "linewidth": 0.9},
        color="#344054",
    )

    # Panel (b): continuous samples and endpoint windows for one period.
    dense_t = np.linspace(0.0, PERIOD_MS, 1800, endpoint=False)
    dense_i = current_response(dense_t)
    ax_i.plot(dense_t, dense_i, color="#d97706", linewidth=2.0, label="Current response (schematic)")

    sample_t = np.concatenate(
        (
            (np.arange(SAMPLES_PER_HALF) + 0.5) * HALF_PERIOD_MS / SAMPLES_PER_HALF,
            HALF_PERIOD_MS
            + (np.arange(SAMPLES_PER_HALF) + 0.5) * HALF_PERIOD_MS / SAMPLES_PER_HALF,
        )
    )
    sample_i = current_response(sample_t)
    ax_i.scatter(
        sample_t,
        sample_i,
        s=12,
        facecolor="white",
        edgecolor="#667085",
        linewidth=0.7,
        zorder=4,
        label="Continuous FIFO samples",
    )

    endpoint_points: list[int] = []
    for half_start in (0.0, HALF_PERIOD_MS):
        win_start, win_end, transition = endpoint_bounds(half_start)
        ax_i.axvspan(win_start, win_end, color="#2f9e44", alpha=0.22, linewidth=0)
        ax_i.axvspan(win_end, transition, color="#d1495b", alpha=0.17, linewidth=0)
        in_window = np.where((sample_t >= win_start) & (sample_t < win_end))[0]
        endpoint_points.extend(in_window.tolist())

    endpoint_points_a = np.asarray(endpoint_points, dtype=int)
    ax_i.scatter(
        sample_t[endpoint_points_a],
        sample_i[endpoint_points_a],
        s=60,
        color="#198754",
        edgecolor="white",
        linewidth=1.0,
        zorder=6,
        label="Selected endpoint sample",
    )

    ax_i.axvline(HALF_PERIOD_MS, color="#222222", linewidth=1.25)
    ax_i.text(HALF_PERIOD_MS / 2.0, -0.43, "Forward half-pulse", ha="center", va="center")
    ax_i.text(1.5 * HALF_PERIOD_MS, -0.43, "Reverse half-pulse", ha="center", va="center")
    ax_i.text(
        PERIOD_MS * 0.51,
        1.38,
        r"$\Delta I_k=\hat I_{f,k}-\hat I_{r,k}$",
        ha="left",
        va="center",
        color="#101828",
        bbox={"boxstyle": "round,pad=0.25", "facecolor": "white", "edgecolor": "#cfd5dd"},
    )
    ax_i.text(
        PERIOD_MS * 0.015,
        1.62,
        "Open circles: continuous FIFO samples\nGreen markers: selected endpoints",
        ha="left",
        va="top",
        color="#475467",
        fontsize=8.9,
    )
    ax_i.text(
        1.15,
        1.18,
        "Current relaxation (schematic)",
        ha="left",
        va="center",
        color="#b85f00",
        fontsize=8.9,
    )

    ax_i.set_xlim(0.0, PERIOD_MS)
    ax_i.set_ylim(-0.52, 1.9)
    ax_i.set_xlabel("Time within one SWV period (ms)")
    ax_i.set_ylabel("Current (illustrative)")
    ax_i.set_yticks([])
    ax_i.set_title("(b) Endpoint extraction from the continuous FIFO record", loc="left", fontweight="bold")
    ax_i.grid(axis="x", color="#d7dde5", linewidth=0.8)
    ax_i.text(
        0.76,
        0.965,
        "Green band: final 5% endpoint window\nRed band: 2% pre-transition guard",
        transform=ax_i.transAxes,
        ha="right",
        va="top",
        fontsize=8.9,
        color="#475467",
    )

    for axis in (ax_v, ax_i):
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.subplots_adjust(left=0.105, right=0.985, top=0.945, bottom=0.09)
    fig.savefig(OUT, dpi=300, facecolor="white")
    print(OUT)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Plot the programmed/exported SWV voltage profile for poster discussion."""

from __future__ import annotations

import csv
import os
from pathlib import Path
from statistics import median

os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib-nanostat")
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


EXPERIMENT_ROOT = Path(__file__).resolve().parents[2]
SENSOR_ROOT = EXPERIMENT_ROOT / "08_sensor_validation" / "raw_and_processed"
NANOSTAT_FILE = SENSOR_ROOT / "sensortest819_nanostat" / "sensor24pbs1000ng.csv"
AUTOLAB_FILE = SENSOR_ROOT / "sensortest820_autolab" / "sensor2pbs1000ng.txt"
OUT_FILE = Path(__file__).resolve().parents[1] / "figures" / "swv_voltage_profile_exported_axis.png"

PULSE_MV = 65 * 0.5372


def read_nanostat_base(path: Path) -> tuple[np.ndarray, float]:
    values: list[float] = []
    frequency_hz: float | None = None
    header: list[str] | None = None
    first_run_id: str | None = None

    with path.open(newline="") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#") or line in {"SUMMARY", "RAW_DATA"}:
                continue
            if "E_WE_RE_mV" in line:
                header = line.split(",")
                continue
            if header is None:
                continue
            row = dict(zip(header, line.split(",")))
            if "run_id" in row:
                if first_run_id is None:
                    first_run_id = row["run_id"]
                if row["run_id"] != first_run_id:
                    continue
            try:
                values.append(float(row["E_WE_RE_mV"]))
                if frequency_hz is None and "frequency_hz" in row:
                    frequency_hz = float(row["frequency_hz"])
            except (KeyError, ValueError):
                continue

    if frequency_hz is None:
        frequency_hz = 120.0
    return np.asarray(values, dtype=float), frequency_hz


def read_autolab(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    base: list[float] = []
    forward: list[float] = []
    time_s: list[float] = []

    with path.open(newline="", errors="replace") as f:
        reader = csv.DictReader(f, delimiter=";")
        for row in reader:
            base.append(float(row["Potential applied (V)"]) * 1000.0)
            forward.append(float(row["WE(1).Forward.Potential (V)"]) * 1000.0)
            time_s.append(float(row["Forward.Time (s)"]))

    return (
        np.asarray(base, dtype=float),
        np.asarray(forward, dtype=float),
        np.asarray(time_s, dtype=float),
    )


def step_stats(values: np.ndarray) -> tuple[int, float, float, float]:
    diffs = np.diff(values)
    return len(values), float(values[0]), float(values[-1]), float(median(diffs))


def swv_trace(base: np.ndarray, frequency_hz: float, n_steps: int) -> tuple[np.ndarray, np.ndarray]:
    period_ms = 1000.0 / frequency_hz
    half_ms = period_ms / 2.0
    xs: list[float] = []
    ys: list[float] = []

    for i, e_base in enumerate(base[:n_steps]):
        t0 = i * period_ms
        # The sign of the two SWV half-pulses depends on the instrument naming.
        # For this figure, the important property is the +/- pulse magnitude
        # around the exported base staircase.
        e_a = e_base + PULSE_MV
        e_b = e_base - PULSE_MV
        xs += [t0, t0 + half_ms, t0 + half_ms, t0 + period_ms]
        ys += [e_a, e_a, e_b, e_b]

    return np.asarray(xs), np.asarray(ys)


def main() -> None:
    nano_base, nano_freq = read_nanostat_base(NANOSTAT_FILE)
    auto_base, auto_forward, auto_time = read_autolab(AUTOLAB_FILE)
    auto_freq = 1.0 / float(median(np.diff(auto_time)))

    n_nano, nano_first, nano_last, nano_step = step_stats(nano_base)
    n_auto, auto_first, auto_last, auto_step = step_stats(auto_base)
    auto_pulse = float(median(auto_forward - auto_base))

    plt.rcParams.update(
        {
            "font.size": 19,
            "axes.titlesize": 25,
            "axes.labelsize": 23,
            "legend.fontsize": 19,
            "xtick.labelsize": 20,
            "ytick.labelsize": 20,
        }
    )

    fig, ax = plt.subplots(figsize=(11.5, 8.9), constrained_layout=False)
    fig.subplots_adjust(left=0.12, right=0.98, top=0.88, bottom=0.31)

    nano_x, nano_y = swv_trace(nano_base, nano_freq, 14)
    auto_x, auto_y = swv_trace(auto_base, auto_freq, 14)

    ax.plot(
        nano_x,
        nano_y,
        color="#1f77b4",
        linewidth=3.3,
        label=f"NanoStat: {n_nano} pts, {nano_step:.2f} mV step",
    )
    ax.plot(
        auto_x,
        auto_y,
        color="#d62728",
        linewidth=3.0,
        linestyle="--",
        label=f"Autolab/NOVA: {n_auto} pts, {auto_step:.2f} mV step",
    )
    ax.set_title("Programmed SWV Voltage Profile")
    ax.set_xlabel("Time from scan start (ms)")
    ax.set_ylabel("E_WE-RE (mV)")
    ax.grid(True, color="#d9e0e8", linewidth=1.1)
    ax.legend(loc="upper right", frameon=True, framealpha=0.94)
    ax.text(
        0.02,
        0.04,
        "Both use ~120 Hz timing.\nNanoStat advances faster because its staircase step is ~5x larger.",
        transform=ax.transAxes,
        fontsize=17,
        color="#344054",
        bbox={"boxstyle": "round,pad=0.35", "facecolor": "white", "edgecolor": "#d0d5dd", "alpha": 0.9},
    )

    text = "\n".join(
        (
        f"Full scan range: NanoStat {nano_first:.1f} to {nano_last:.1f} mV, "
        f"{n_nano} exported points, f={nano_freq:.0f} Hz.",
        f"Autolab {auto_first:.1f} to {auto_last:.1f} mV, {n_auto} exported points, "
        f"f={auto_freq:.0f} Hz, pulse offset {auto_pulse:.1f} mV.",
        "Note: NanoStat profile is reconstructed from firmware settings and the exported potential axis;",
        "it is not an oscilloscope measurement of WE-RE."
        )
    )
    fig.text(0.02, 0.055, text, ha="left", va="bottom", fontsize=14, color="#344054")

    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT_FILE, dpi=300)
    print(OUT_FILE)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate paired NanoStat/Autolab PBS SWV plots for the thesis.

This script reuses the parsing and analysis logic from
`plot_cortisol_change_curves.py`, which mirrors the browser analysis
app: SG5 current, auto two-shoulder linear baseline, and the same peak window.

Sensors 2, 4, and 6 are shown in matched columns. NanoStat and Autolab occupy
the two rows so that each sensor uses the same current scale across instruments.
"""

from __future__ import annotations

import importlib.util
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


ROOT = Path(__file__).resolve().parent
DATA_ROOT = ROOT / "raw_and_processed"
NANOSTAT_DIR = DATA_ROOT / "sensortest819_nanostat"
AUTOLAB_DIR = DATA_ROOT / "sensortest820_autolab"
ANALYSIS_SCRIPT = ROOT / "plot_cortisol_change_curves.py"
OUT_DIR = ROOT / "analysis_outputs" / "poster_assets"
OUT_PATH = OUT_DIR / "sensor246_nanostat_vs_autolab_sg5_shoulder.png"


spec = importlib.util.spec_from_file_location("curve_analysis", ANALYSIS_SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Cannot import analysis script: {ANALYSIS_SCRIPT}")
analysis = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = analysis
spec.loader.exec_module(analysis)


SENSORS = ("2", "4", "6")
INSTRUMENTS = ("NanoStat", "Autolab")
CONC_ORDER = [0.0, 0.1, 1.0, 10.0, 100.0, 1000.0]
COLORS = {
    0.0: "#aeb7c2",
    0.1: "#6b7280",
    1.0: "#b87514",
    10.0: "#8a4bd3",
    100.0: "#16833a",
    1000.0: "#d24b35",
}

TITLE_SIZE = 19
PANEL_TITLE_SIZE = 14
AXIS_LABEL_SIZE = 12
TICK_LABEL_SIZE = 10
LEGEND_SIZE = 10.5
FOOTNOTE_SIZE = 9.0


def padded_ylim(values):
    if not values:
        return (-1.0, 1.0)
    lo = min(values)
    hi = max(values)
    lo = min(lo, 0.0)
    hi = max(hi, 0.0)
    span = hi - lo
    if span <= 0:
        span = max(abs(hi), 1.0)
    pad = span * 0.10
    return (lo - pad, hi + pad)


def corrected_curve(run):
    pts = list(zip(run.voltage_mv, run.current_sg5_na))
    baseline_pts = [
        (x, y)
        for x, y in pts
        if analysis.in_window(x, analysis.LEFT_BASELINE_MV)
        or analysis.in_window(x, analysis.RIGHT_BASELINE_MV)
    ]
    slope, intercept = analysis.lin_fit(baseline_pts)
    return [x for x, _ in pts], [y - (slope * x + intercept) for x, y in pts]


def collect_sensor246_runs():
    runs = []

    for pattern in (
        "sensor24blankpbsrun*.csv",
        "sensor24pbs*ng.csv",
        "sensor56blankpbsrun*.csv",
        "sensor56pbs*ng.csv",
    ):
        for path in sorted(NANOSTAT_DIR.glob(pattern)):
            runs.extend(
                analysis.parse_nanostat_csv(
                    path,
                    "nanostat_2026-08-19_sensors2456",
                    "NanoStat",
                )
            )

    for path in sorted(AUTOLAB_DIR.glob("sensor*pbs*.txt")):
        runs.extend(
            analysis.parse_nova_txt(
                path,
                "autolab_2026-08-20_sensors1246",
                "Autolab",
            )
        )

    return [
        run
        for run in runs
        if run.sensor in SENSORS
        and run.instrument in INSTRUMENTS
        and (run.kind == "blank" or math.isfinite(run.concentration))
    ]


def conc_label(conc: float) -> str:
    if conc == 0.0:
        return "blank"
    if conc < 1.0:
        return f"{conc:g} ng/mL"
    return f"{conc:.0f} ng/mL"


def plot():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    runs = collect_sensor246_runs()

    fig, axes = plt.subplots(
        nrows=len(INSTRUMENTS),
        ncols=len(SENSORS),
        figsize=(15.3, 8.4),
        sharex=True,
        sharey=False,
        constrained_layout=False,
    )

    for col, sensor in enumerate(SENSORS):
        sensor_y_values = []
        for row, instrument in enumerate(INSTRUMENTS):
            ax = axes[row][col]
            subset = [
                run for run in runs
                if run.sensor == sensor and run.instrument == instrument
            ]
            subset.sort(key=lambda r: (
                0 if r.kind == "blank" else 1,
                r.concentration,
                r.file_name,
                r.run_id,
            ))

            for run in subset:
                voltage, current = corrected_curve(run)
                sensor_y_values.extend(
                    y for x, y in zip(voltage, current) if -430.0 <= x <= 5.0
                )
                conc = 0.0 if run.kind == "blank" else run.concentration
                is_blank = run.kind == "blank"
                color = COLORS.get(conc, "#111827")
                label = conc_label(conc)
                if is_blank:
                    label = "blank"
                ax.plot(
                    voltage,
                    current,
                    color=color,
                    linewidth=1.0 if is_blank else 1.8,
                    alpha=0.48 if is_blank else 0.95,
                    label=label,
                )

            ax.axvspan(
                analysis.PEAK_WINDOW_MV[0],
                analysis.PEAK_WINDOW_MV[1],
                color="#cfe8dc",
                alpha=0.48,
                linewidth=0,
            )
            ax.axhline(0, color="#94a3b8", linewidth=0.8, alpha=0.7)
            ax.grid(True, color="#d9e1ec", linewidth=0.8, alpha=0.85)
            ax.set_xlim(10, -460)
            ax.set_title(f"Sensor {sensor} - {instrument}", fontsize=PANEL_TITLE_SIZE, weight="bold")
            if col == 0:
                ax.set_ylabel("Baseline-subtracted current (nA)", fontsize=AXIS_LABEL_SIZE)
            if row == len(INSTRUMENTS) - 1:
                ax.set_xlabel("E_WE-RE (mV)", fontsize=AXIS_LABEL_SIZE)
            ax.tick_params(axis="both", labelsize=TICK_LABEL_SIZE)

            if not subset:
                ax.text(
                    0.5,
                    0.5,
                    "No runs found",
                    transform=ax.transAxes,
                    ha="center",
                    va="center",
                    color="#64748b",
                )

        sensor_ylim = padded_ylim(sensor_y_values)
        for row in range(len(INSTRUMENTS)):
            axes[row][col].set_ylim(sensor_ylim)

    legend_handles = [
        Line2D(
            [0],
            [0],
            color=COLORS[conc],
            linewidth=1.0 if conc == 0.0 else 1.8,
            alpha=0.60 if conc == 0.0 else 0.95,
            label=conc_label(conc),
        )
        for conc in CONC_ORDER
    ]
    fig.legend(
        handles=legend_handles,
        loc="upper center",
        bbox_to_anchor=(0.5, 0.925),
        ncol=len(CONC_ORDER),
        fontsize=LEGEND_SIZE,
        frameon=True,
        framealpha=0.90,
        columnspacing=1.4,
        handlelength=2.2,
    )

    fig.suptitle(
        "Sensors 2, 4 and 6: NanoStat vs Autolab PBS SWV comparison",
        fontsize=TITLE_SIZE,
        weight="bold",
        y=0.985,
    )
    fig.subplots_adjust(left=0.070, right=0.985, top=0.855, bottom=0.115, hspace=0.25, wspace=0.14)
    fig.text(
        0.5,
        0.026,
        "Processing: SG5 current, auto two-shoulder linear baseline "
        f"({analysis.LEFT_BASELINE_MV[0]:.0f}..{analysis.LEFT_BASELINE_MV[1]:.0f} mV and "
        f"{analysis.RIGHT_BASELINE_MV[0]:.0f}..{analysis.RIGHT_BASELINE_MV[1]:.0f} mV); "
        f"green band = peak window {analysis.PEAK_WINDOW_MV[0]:.0f}..{analysis.PEAK_WINDOW_MV[1]:.0f} mV. "
        "Each sensor column uses one shared y-scale.",
        ha="center",
        fontsize=FOOTNOTE_SIZE,
        color="#475569",
    )
    fig.savefig(OUT_PATH, dpi=240, bbox_inches="tight")
    print(OUT_PATH)
    print(f"Plotted {len(runs)} runs")


if __name__ == "__main__":
    plot()

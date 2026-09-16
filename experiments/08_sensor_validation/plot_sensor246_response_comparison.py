#!/usr/bin/env python3
"""Generate a poster-style concentration response comparison for sensors 2/4/6.

The poster version places NanoStat and Autolab on the same axes. Sensor identity
is encoded by colour, while instrument/day is encoded by line style:
NanoStat is solid and Autolab is dashed.
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
ANALYSIS_SCRIPT = ROOT / "plot_cortisol_change_curves.py"
OUT_DIR = ROOT / "analysis_outputs" / "poster_assets"
OUT_PATH = OUT_DIR / "sensor246_nanostat_autolab_concentration_response_sg5.png"


spec = importlib.util.spec_from_file_location("curve_analysis", ANALYSIS_SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Cannot import analysis script: {ANALYSIS_SCRIPT}")
analysis = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = analysis
spec.loader.exec_module(analysis)


DATASETS = (
    "nanostat_2026-08-19_sensors2456",
    "autolab_2026-08-20_sensors1246",
)
SENSORS = ("2", "4", "6")
MIN_PLOTTED_CONC_NG_ML = 1.0
COLORS = {
    "2": "#2563eb",
    "4": "#16a34a",
    "6": "#dc2626",
}
MARKERS = {
    "2": "o",
    "4": "s",
    "6": "^",
}

TITLE_SIZE = 18
AXIS_LABEL_SIZE = 13
TICK_LABEL_SIZE = 12
LEGEND_SIZE = 10.5
FOOTNOTE_SIZE = 9.4


LINESTYLES = {
    "nanostat_2026-08-19_sensors2456": "-",
    "autolab_2026-08-20_sensors1246": "--",
}

DATASET_LABELS = {
    "nanostat_2026-08-19_sensors2456": "NanoStat, Day 1",
    "autolab_2026-08-20_sensors1246": "Autolab, Day 2",
}


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    runs = [
        run for run in analysis.collect_runs()
        if run.dataset in DATASETS and run.sensor in SENSORS
    ]
    metrics = [analysis.compute_metric(run) for run in runs]
    analysis.add_percent_changes(metrics)
    response_rows = analysis.aggregate_response(metrics)

    fig, ax = plt.subplots(figsize=(10.6, 6.3), dpi=240)

    for sensor in SENSORS:
        for dataset in DATASETS:
            sensor_rows = sorted(
                [
                    row for row in response_rows
                    if row["dataset"] == dataset and str(row["sensor"]) == sensor
                    and float(row["concentration_ng_mL"]) >= MIN_PLOTTED_CONC_NG_ML
                ],
                key=lambda row: float(row["concentration_ng_mL"]),
            )
            if not sensor_rows:
                continue
            x = [float(row["concentration_ng_mL"]) for row in sensor_rows]
            y = [float(row["change_pct_mean"]) for row in sensor_rows]
            yerr = [
                float(row["change_pct_sd"]) if int(row["n"]) > 1 else 0.0
                for row in sensor_rows
            ]
            ax.errorbar(
                x,
                y,
                yerr=yerr,
                marker=MARKERS[sensor],
                markersize=7.5,
                linewidth=2.4,
                capsize=3.5,
                linestyle=LINESTYLES[dataset],
                color=COLORS[sensor],
                label=f"S{sensor} {DATASET_LABELS[dataset]}",
            )

    ax.axhline(0, color="#64748b", linewidth=1.0, alpha=0.75)
    ax.set_xscale("log")
    ax.set_xlim(0.8, 1500)
    ax.set_xlabel("Cortisol concentration in PBS (ng/mL)", fontsize=AXIS_LABEL_SIZE)
    ax.set_ylabel("Change in peak current relative to blank (%)", fontsize=AXIS_LABEL_SIZE)
    ax.tick_params(axis="both", labelsize=TICK_LABEL_SIZE)
    ax.grid(True, which="both", color="#d9e1ec", linewidth=0.8, alpha=0.82)

    sensor_handles = [
        Line2D([0], [0], color=COLORS[sensor], marker=MARKERS[sensor],
               linewidth=2.4, markersize=7.5, label=f"Sensor {sensor}")
        for sensor in SENSORS
    ]
    style_handles = [
        Line2D([0], [0], color="#111827", linestyle="-", linewidth=2.4, label="NanoStat, Day 1"),
        Line2D([0], [0], color="#111827", linestyle="--", linewidth=2.4, label="Autolab, Day 2"),
    ]
    legend1 = ax.legend(
        handles=sensor_handles,
        loc="upper left",
        fontsize=LEGEND_SIZE,
        frameon=True,
        framealpha=0.92,
        title="Sensor",
        title_fontsize=LEGEND_SIZE,
    )
    ax.add_artist(legend1)
    ax.legend(
        handles=style_handles,
        loc="lower right",
        fontsize=LEGEND_SIZE,
        frameon=True,
        framealpha=0.92,
        title="Instrument",
        title_fontsize=LEGEND_SIZE,
    )

    fig.suptitle(
        "PBS Concentration Response: NanoStat vs Autolab",
        fontsize=TITLE_SIZE,
        weight="bold",
        y=0.965,
    )
    fig.text(
        0.5,
        0.025,
        "Peak height extracted from baseline-subtracted SG5 current in -300..-200 mV; "
        "change calculated relative to each sensor's blank mean.",
        ha="center",
        fontsize=FOOTNOTE_SIZE,
        color="#475569",
    )
    fig.subplots_adjust(left=0.115, right=0.985, top=0.885, bottom=0.165)
    fig.savefig(OUT_PATH)
    print(OUT_PATH)


if __name__ == "__main__":
    main()

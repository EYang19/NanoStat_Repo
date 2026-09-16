#!/usr/bin/env python3
"""Generate an annotated SWV peak-extraction example figure.

The figure uses the same analysis convention as the browser app:
SG5 current, two-shoulder linear baseline, and peak extraction from
the baseline-subtracted trace within the selected peak window.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parent
DATA_ROOT = ROOT / "raw_and_processed"
ANALYSIS_SCRIPT = ROOT / "plot_cortisol_change_curves.py"
OUT_DIR = ROOT / "analysis_outputs" / "poster_assets"
OUT_PATH = OUT_DIR / "swv_peak_extraction_example_sg5.png"

TITLE_SIZE = 18
AXIS_LABEL_SIZE = 14
TICK_LABEL_SIZE = 12
LEGEND_SIZE = 11
ANNOTATION_SIZE = 12
SHOULDER_LABEL_SIZE = 11
FOOTNOTE_SIZE = 9.5


spec = importlib.util.spec_from_file_location("curve_analysis", ANALYSIS_SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Cannot import analysis script: {ANALYSIS_SCRIPT}")
analysis = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = analysis
spec.loader.exec_module(analysis)


def corrected_curve(run):
    pts = list(zip(run.voltage_mv, run.current_sg5_na))
    baseline_pts = [
        (x, y)
        for x, y in pts
        if analysis.in_window(x, analysis.LEFT_BASELINE_MV)
        or analysis.in_window(x, analysis.RIGHT_BASELINE_MV)
    ]
    slope, intercept = analysis.lin_fit(baseline_pts)
    voltage = [x for x, _ in pts]
    current = [y for _, y in pts]
    baseline = [slope * x + intercept for x in voltage]
    corrected = [y - b for y, b in zip(current, baseline)]
    return voltage, current, baseline, corrected


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    source_path = DATA_ROOT / "sensortest819_nanostat" / "sensor24pbs1000ng.csv"
    runs = analysis.parse_nanostat_csv(
        source_path,
        "nanostat_2026-08-19_sensors2456",
        "NanoStat",
    )
    run = next(r for r in runs if r.sensor == "2")

    voltage, current, baseline, corrected = corrected_curve(run)
    peak_candidates = [
        (x, y)
        for x, y in zip(voltage, corrected)
        if analysis.in_window(x, analysis.PEAK_WINDOW_MV)
    ]
    peak_v, peak_i = max(peak_candidates, key=lambda item: item[1])

    fig, ax = plt.subplots(figsize=(8.4, 4.8))
    ax.set_title("SWV Peak Extraction Example", fontsize=TITLE_SIZE, weight="bold", pad=12)

    ax.axvspan(
        analysis.LEFT_BASELINE_MV[0],
        analysis.LEFT_BASELINE_MV[1],
        color="#dbeafe",
        alpha=0.55,
        linewidth=0,
        label="baseline windows",
    )
    ax.axvspan(
        analysis.RIGHT_BASELINE_MV[0],
        analysis.RIGHT_BASELINE_MV[1],
        color="#dbeafe",
        alpha=0.55,
        linewidth=0,
    )
    ax.axvspan(
        analysis.PEAK_WINDOW_MV[0],
        analysis.PEAK_WINDOW_MV[1],
        color="#dcfce7",
        alpha=0.58,
        linewidth=0,
        label="peak window",
    )

    ax.plot(voltage, current, color="#2563eb", linewidth=2.0, label="SG5 SWV current")
    ax.plot(voltage, baseline, color="#ef4444", linewidth=1.8, linestyle="--", label="two-shoulder baseline")
    ax.plot(voltage, corrected, color="#111827", linewidth=2.0, label="baseline-subtracted signal")
    ax.axhline(0, color="#94a3b8", linewidth=0.9)

    ax.scatter([peak_v], [peak_i], s=58, color="#111827", zorder=5)
    ax.vlines(peak_v, 0, peak_i, color="#111827", linewidth=1.5)
    ax.annotate(
        f"Peak height = {peak_i:.1f} nA",
        xy=(peak_v, peak_i),
        xytext=(-335, peak_i + 58),
        arrowprops={"arrowstyle": "->", "color": "#111827", "lw": 1.3},
        fontsize=ANNOTATION_SIZE,
        color="#111827",
    )
    ax.annotate(
        f"Peak potential = {peak_v:.0f} mV",
        xy=(peak_v, peak_i),
        xytext=(-175, peak_i + 15),
        arrowprops={"arrowstyle": "->", "color": "#111827", "lw": 1.3},
        fontsize=ANNOTATION_SIZE,
        color="#111827",
    )
    ax.text(
        -365,
        min(current) + 10,
        "negative shoulder",
        fontsize=SHOULDER_LABEL_SIZE,
        color="#1d4ed8",
        ha="center",
    )
    ax.text(
        -135,
        min(current) + 10,
        "positive shoulder",
        fontsize=SHOULDER_LABEL_SIZE,
        color="#1d4ed8",
        ha="center",
    )

    ax.set_xlim(10, -460)
    ax.set_xlabel("E_WE-RE (mV)", fontsize=AXIS_LABEL_SIZE)
    ax.set_ylabel("Current / extracted current (nA)", fontsize=AXIS_LABEL_SIZE)
    ax.tick_params(axis="both", labelsize=TICK_LABEL_SIZE)
    ax.grid(True, color="#d9e1ec", linewidth=0.8, alpha=0.85)
    ax.legend(loc="upper left", fontsize=LEGEND_SIZE, frameon=True, framealpha=0.92)
    fig.text(
        0.5,
        0.02,
        "SG5 current; two-shoulder baseline; peak = maximum baseline-subtracted signal in -300..-200 mV.",
        ha="center",
        fontsize=FOOTNOTE_SIZE,
        color="#475569",
    )
    fig.subplots_adjust(left=0.12, right=0.98, top=0.86, bottom=0.20)
    fig.savefig(OUT_PATH, dpi=240)
    print(OUT_PATH)


if __name__ == "__main__":
    main()

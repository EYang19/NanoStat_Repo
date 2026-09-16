#!/usr/bin/env python3
"""Analyse matched PBS blank scans and the earlier Sensor 1/3/5 dataset.

The processing matches the report's existing sensor analysis:
  * NanoStat uses the exported SG5 current.
  * Autolab delta current is smoothed with the same five-point SG kernel.
  * A line fitted to the -400..-330 mV and -170..-100 mV shoulders is removed.
  * Peak height is the maximum corrected current in -300..-200 mV.

Raw source files are never modified. Outputs are written to the adjacent
``analysis`` directory.
"""

from __future__ import annotations

import csv
import importlib.util
import math
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean, stdev

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


DATA_ROOT = Path(__file__).resolve().parent
NANOSTAT_DIR = DATA_ROOT / "sensortest819_nanostat"
AUTOLAB_DIR = DATA_ROOT / "sensortest820_autolab"
EARLY_DIR = DATA_ROOT / "earlystagenanostatdata"
OUT_DIR = DATA_ROOT / "analysis"
CORE_SCRIPT = DATA_ROOT.parent / "plot_cortisol_change_curves.py"

spec = importlib.util.spec_from_file_location("sensor_core", CORE_SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Cannot import analysis code from {CORE_SCRIPT}")
core = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = core
spec.loader.exec_module(core)

MATCHED_SENSORS = ("2", "4", "6")
EARLY_SENSORS = ("1", "3", "5")
INSTRUMENTS = ("NanoStat", "Autolab")
INSTRUMENT_COLORS = {"NanoStat": "#2563eb", "Autolab": "#c2410c"}
SENSOR_COLORS = {"1": "#2563eb", "3": "#16833a", "5": "#b87514"}


def sample_sd(values: list[float]) -> float:
    return stdev(values) if len(values) >= 2 else 0.0


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        raise ValueError(f"No rows available for {path}")
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def corrected_curve(run) -> tuple[list[float], list[float]]:
    points = list(zip(run.voltage_mv, run.current_sg5_na))
    shoulders = [
        (x, y)
        for x, y in points
        if core.in_window(x, core.LEFT_BASELINE_MV)
        or core.in_window(x, core.RIGHT_BASELINE_MV)
    ]
    slope, intercept = core.lin_fit(shoulders)
    return (
        [x for x, _ in points],
        [y - (slope * x + intercept) for x, y in points],
    )


def collect_matched_blank_runs() -> list:
    runs = []
    for pattern in ("sensor24blankpbsrun*.csv", "sensor56blankpbsrun*.csv"):
        for path in sorted(NANOSTAT_DIR.glob(pattern)):
            runs.extend(core.parse_nanostat_csv(path, "C2_2026-08-19", "NanoStat"))
    for path in sorted(AUTOLAB_DIR.glob("sensor*pbsblankrun*.txt")):
        runs.extend(core.parse_nova_txt(path, "C2_2026-08-20", "Autolab"))
    return [run for run in runs if run.sensor in MATCHED_SENSORS and run.kind == "blank"]


def matched_blank_metrics(runs: list) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    metric_rows: list[dict[str, object]] = []
    by_group: dict[tuple[str, str], list] = defaultdict(list)

    for run in runs:
        metric = core.compute_metric(run)
        metric_rows.append({
            "campaign": metric.dataset,
            "instrument": metric.instrument,
            "sensor": int(metric.sensor),
            "source_file": metric.file_name,
            "run_id": metric.run_id,
            "peak_height_nA": metric.peak_height_nA,
            "peak_voltage_mV": metric.peak_voltage_mV,
            "baseline_sigma_nA": metric.baseline_sigma_nA,
        })
        by_group[(metric.instrument, metric.sensor)].append(metric)

    metric_rows.sort(key=lambda row: (
        INSTRUMENTS.index(str(row["instrument"])),
        int(row["sensor"]),
        str(row["source_file"]),
        str(row["run_id"]),
    ))

    summary_rows: list[dict[str, object]] = []
    for instrument in INSTRUMENTS:
        for sensor in MATCHED_SENSORS:
            group = by_group[(instrument, sensor)]
            heights = [metric.peak_height_nA for metric in group]
            voltages = [metric.peak_voltage_mV for metric in group]
            sigmas = [metric.baseline_sigma_nA for metric in group]
            height_mean = mean(heights)
            height_sd = sample_sd(heights)
            summary_rows.append({
                "instrument": instrument,
                "sensor": int(sensor),
                "n": len(group),
                "peak_height_mean_nA": height_mean,
                "peak_height_run_to_run_sd_nA": height_sd,
                "peak_height_cv_pct": 100.0 * height_sd / abs(height_mean),
                "peak_voltage_mean_mV": mean(voltages),
                "peak_voltage_sd_mV": sample_sd(voltages),
                "baseline_sigma_mean_nA": mean(sigmas),
            })
    return metric_rows, summary_rows


def plot_matched_blank_overlay(runs: list, summary_rows: list[dict[str, object]]) -> Path:
    fig, axes = plt.subplots(2, 3, figsize=(11.2, 6.8), sharex=True, constrained_layout=False)
    summary = {(str(row["instrument"]), str(row["sensor"])): row for row in summary_rows}

    curves: dict[tuple[str, str], list[tuple[list[float], list[float], object]]] = defaultdict(list)
    for run in runs:
        voltage, current = corrected_curve(run)
        curves[(run.instrument, run.sensor)].append((voltage, current, run))

    for col, sensor in enumerate(MATCHED_SENSORS):
        sensor_values = []
        for instrument in INSTRUMENTS:
            for voltage, current, _ in curves[(instrument, sensor)]:
                sensor_values.extend(
                    y for x, y in zip(voltage, current) if -450.0 <= x <= 0.0
                )
        low = min(sensor_values)
        high = max(sensor_values)
        pad = 0.08 * (high - low)

        for row, instrument in enumerate(INSTRUMENTS):
            ax = axes[row, col]
            group = sorted(
                curves[(instrument, sensor)],
                key=lambda item: (item[2].file_name, item[2].run_id),
            )
            for index, (voltage, current, _) in enumerate(group, start=1):
                ax.plot(
                    voltage,
                    current,
                    color=INSTRUMENT_COLORS[instrument],
                    linewidth=1.35,
                    alpha=0.38 + 0.11 * min(index, 5),
                    label=f"Run {index}",
                )
            ax.axvspan(*core.PEAK_WINDOW_MV, color="#bbdfc8", alpha=0.24, linewidth=0)
            ax.axhline(0.0, color="#64748b", linewidth=0.75, alpha=0.65)
            ax.set_xlim(-450.0, 0.0)
            ax.set_ylim(low - pad, high + pad)
            ax.grid(True, color="#d8dee8", linewidth=0.65, alpha=0.75)
            ax.tick_params(labelsize=8.5)
            stats = summary[(instrument, sensor)]
            ax.set_title(f"Sensor {sensor} - {instrument}", fontsize=10.5, weight="bold")
            ax.text(
                0.03,
                0.95,
                (f"n={stats['n']}; peak={float(stats['peak_height_mean_nA']):.1f}"
                 f" +/- {float(stats['peak_height_run_to_run_sd_nA']):.1f} nA\n"
                 f"CV={float(stats['peak_height_cv_pct']):.1f}%"),
                transform=ax.transAxes,
                va="top",
                fontsize=8.2,
                bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.80, "pad": 2.5},
            )
            if col == 0:
                ax.set_ylabel("Baseline-subtracted SG5 current (nA)", fontsize=9.5)
            if row == 1:
                ax.set_xlabel("Potential, E_WE-RE (mV)", fontsize=9.5)
            ax.legend(loc="lower right", fontsize=7.2, frameon=True, framealpha=0.84,
                      ncol=2 if len(group) > 3 else 1, handlelength=1.6,
                      columnspacing=0.7, labelspacing=0.2)

    fig.suptitle("Matched C2 PBS blank scans: NanoStat and Autolab", fontsize=14, weight="bold", y=0.985)
    fig.text(
        0.5,
        0.018,
        "SG5; two-shoulder linear baseline (-400..-330 and -170..-100 mV); "
        "shaded region = -300..-200 mV peak window. Each sensor column uses one shared y-scale.",
        ha="center",
        fontsize=8.4,
        color="#475569",
    )
    fig.subplots_adjust(left=0.075, right=0.985, top=0.91, bottom=0.105, hspace=0.28, wspace=0.17)
    out = OUT_DIR / "sensor246_blank_overlay.png"
    fig.savefig(out, dpi=300)
    plt.close(fig)
    return out


def collect_early_runs() -> list:
    runs = []
    for path in sorted(EARLY_DIR.glob("pbsblank135run*.csv")):
        runs.extend(core.parse_nanostat_csv(path, "C1_development", "NanoStat"))
    for path in sorted(EARLY_DIR.glob("pbs*ng135.csv")):
        runs.extend(core.parse_nanostat_csv(path, "C1_development", "NanoStat"))
    return [run for run in runs if run.sensor in EARLY_SENSORS]


def plot_early_response(runs: list) -> tuple[Path, list[dict[str, object]]]:
    metrics = [core.compute_metric(run) for run in runs]
    core.add_percent_changes(metrics)
    response_rows = core.aggregate_response(metrics)
    blank_summary = {}
    for sensor in EARLY_SENSORS:
        blanks = [m.peak_height_nA for m in metrics if m.sensor == sensor and m.kind == "blank"]
        blank_summary[sensor] = (mean(blanks), sample_sd(blanks))

    fig, axes = plt.subplots(1, 3, figsize=(11.2, 3.7), sharey=True)
    for ax, sensor in zip(axes, EARLY_SENSORS):
        rows = sorted(
            [row for row in response_rows if str(row["sensor"]) == sensor],
            key=lambda row: float(row["concentration_ng_mL"]),
        )
        main_rows = [row for row in rows if float(row["concentration_ng_mL"]) <= 1000.0]
        x = [float(row["concentration_ng_mL"]) for row in main_rows]
        y = [float(row["change_pct_mean"]) for row in main_rows]
        yerr = [float(row["change_pct_sd"]) if int(row["n"]) > 1 else 0.0 for row in main_rows]
        ax.errorbar(x, y, yerr=yerr, marker="o", linewidth=1.9, markersize=5.5,
                    capsize=3, color=SENSOR_COLORS[sensor], label="1-1000 ng/mL")

        extra = [row for row in rows if float(row["concentration_ng_mL"]) > 1000.0]
        if extra:
            row = extra[0]
            ex = float(row["concentration_ng_mL"])
            ey = float(row["change_pct_mean"])
            eyerr = float(row["change_pct_sd"]) if int(row["n"]) > 1 else 0.0
            ax.plot([x[-1], ex], [y[-1], ey], linestyle="--", linewidth=1.0,
                    color=SENSOR_COLORS[sensor], alpha=0.65)
            ax.errorbar([ex], [ey], yerr=[eyerr], marker="s", markerfacecolor="white",
                        markeredgecolor=SENSOR_COLORS[sensor], capsize=3, linestyle="none",
                        label="10,000 ng/mL check")

        blank_mean, blank_sd = blank_summary[sensor]
        ax.axhline(0.0, color="#64748b", linewidth=0.8)
        ax.set_xscale("log")
        ax.set_xlim(0.75, 15000.0)
        ax.grid(True, which="both", color="#d8dee8", linewidth=0.65, alpha=0.75)
        ax.set_title(f"Sensor {sensor}", fontsize=11, weight="bold")
        ax.set_xlabel("Cortisol in PBS (ng/mL)", fontsize=9.5)
        ax.tick_params(labelsize=8.5)
        ax.text(
            0.04,
            0.94,
            f"blank: {blank_mean:.1f} +/- {blank_sd:.1f} nA\nCV={100.0 * blank_sd / abs(blank_mean):.1f}%",
            transform=ax.transAxes,
            va="top",
            fontsize=8.2,
            bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.82, "pad": 2.5},
        )
        ax.legend(loc="lower right", fontsize=7.3, framealpha=0.86)

    axes[0].set_ylabel("Peak-current change from blank (%)", fontsize=9.5)
    fig.suptitle("C1 NanoStat development dataset: Sensors 1, 3 and 5", fontsize=13.5, weight="bold", y=0.985)
    fig.text(
        0.5,
        0.015,
        "Previously used sensors; SG5 and the same two-shoulder baseline/peak window as the C2 analysis. "
        "The 10,000 ng/mL points are shown as development checks.",
        ha="center",
        fontsize=8.2,
        color="#475569",
    )
    fig.subplots_adjust(left=0.075, right=0.985, top=0.84, bottom=0.20, wspace=0.13)
    out = OUT_DIR / "sensor135_development_response.png"
    fig.savefig(out, dpi=300)
    plt.close(fig)
    return out, response_rows


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    matched_runs = collect_matched_blank_runs()
    per_run_rows, summary_rows = matched_blank_metrics(matched_runs)
    write_csv(OUT_DIR / "sensor246_blank_per_run.csv", per_run_rows)
    write_csv(OUT_DIR / "sensor246_blank_summary.csv", summary_rows)
    overlay_path = plot_matched_blank_overlay(matched_runs, summary_rows)

    early_runs = collect_early_runs()
    early_path, early_rows = plot_early_response(early_runs)

    print(f"Matched blank runs: {len(matched_runs)}")
    for row in summary_rows:
        print(row)
    print(f"C1 development runs: {len(early_runs)}; response groups: {len(early_rows)}")
    print(overlay_path)
    print(early_path)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Plot PBS blank SG5 curves for sensors 1, 3, and 5.

Input assumption for raw_and_processed/earlystagenanostatdata/pbsblank135run*.csv:
each file is one experimental round, and the three CSV run_id values were
measured in physical sensor order 1, 3, 5.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from statistics import mean, stdev

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


HERE = Path(__file__).resolve().parent
DEFAULT_DATA_DIR = HERE / "raw_and_processed" / "earlystagenanostatdata"
DEFAULT_OUT_DIR = HERE / "analysis_outputs" / "pbsblank135_plots"
SENSOR_BY_RUN_ID = {1: "1", 2: "3", 3: "5"}
PEAK_WINDOW_MV = (-280.0, -220.0)
BASELINE_WINDOW_MV = (-450.0, -330.0)
WIDE_BASELINE_WINDOW_MV = (-400.0, -100.0)


def sample_sd(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    return stdev(values)


def parse_multirun_csv(path: Path) -> dict[int, list[dict[str, str]]]:
    raw_by_run: dict[int, list[dict[str, str]]] = {}
    in_raw = False
    header: list[str] | None = None

    with path.open(newline="") as handle:
        for row in csv.reader(handle):
            if not row:
                continue
            if row[0] == "RAW_DATA":
                in_raw = True
                header = None
                continue
            if not in_raw or row[0].startswith("#") or row[0] == "SUMMARY":
                continue
            if header is None:
                header = row
                continue

            record = dict(zip(header, row))
            raw_by_run.setdefault(int(record["run_id"]), []).append(record)

    return raw_by_run


def round_index(path: Path) -> int:
    match = re.search(r"run(\d+)", path.stem)
    if match is None:
        raise ValueError(f"Cannot infer round index from {path.name}")
    return int(match.group(1))


def load_sensor_curves(data_dir: Path) -> dict[str, list[tuple[int, list[float], list[float]]]]:
    curves: dict[str, list[tuple[int, list[float], list[float]]]] = {
        sensor: [] for sensor in SENSOR_BY_RUN_ID.values()
    }

    for path in sorted(data_dir.glob("pbsblank135run*.csv"), key=round_index):
        round_no = round_index(path)
        raw_by_run = parse_multirun_csv(path)

        for run_id, sensor in SENSOR_BY_RUN_ID.items():
            rows = raw_by_run.get(run_id, [])
            if not rows:
                print(f"WARNING: {path.name} has no run_id {run_id} for sensor {sensor}")
                continue

            voltage_mv = [float(row["E_WE_RE_mV"]) for row in rows]
            sg5_na = [float(row["Delta_I_sg5_nA"]) for row in rows]
            curves[sensor].append((round_no, voltage_mv, sg5_na))

    return curves


def peak_metrics(round_no: int,
                 voltage_mv: list[float],
                 sg5_na: list[float]) -> dict[str, float | int]:
    baseline_points = [
        current
        for voltage, current in zip(voltage_mv, sg5_na)
        if BASELINE_WINDOW_MV[0] <= voltage <= BASELINE_WINDOW_MV[1]
    ]
    baseline_mean = mean(baseline_points)
    baseline_sd = sample_sd(baseline_points)

    peak_points = [
        (voltage, current - baseline_mean)
        for voltage, current in zip(voltage_mv, sg5_na)
        if PEAK_WINDOW_MV[0] <= voltage <= PEAK_WINDOW_MV[1]
    ]
    peak_voltage, peak_height = max(peak_points, key=lambda item: item[1])

    return {
        "round": round_no,
        "baseline_mean_nA": baseline_mean,
        "baseline_sd_nA": baseline_sd,
        "peak_height_nA": peak_height,
        "peak_voltage_mV": peak_voltage,
        "snr": abs(peak_height) / baseline_sd if baseline_sd > 0 else float("inf"),
    }


def linear_fit(x_values: list[float], y_values: list[float]) -> tuple[float, float]:
    x_mean = mean(x_values)
    y_mean = mean(y_values)
    numerator = sum((x - x_mean) * (y - y_mean) for x, y in zip(x_values, y_values))
    denominator = sum((x - x_mean) ** 2 for x in x_values)
    slope = numerator / denominator if denominator else 0.0
    intercept = y_mean - slope * x_mean
    return slope, intercept


def wide_baseline_peak_metrics(round_no: int,
                               voltage_mv: list[float],
                               sg5_na: list[float]) -> dict[str, float | int]:
    fit_points = [
        (voltage, current)
        for voltage, current in zip(voltage_mv, sg5_na)
        if WIDE_BASELINE_WINDOW_MV[0] <= voltage <= WIDE_BASELINE_WINDOW_MV[1]
    ]
    fit_x = [point[0] for point in fit_points]
    fit_y = [point[1] for point in fit_points]
    slope, intercept = linear_fit(fit_x, fit_y)
    residuals = [current - (slope * voltage + intercept) for voltage, current in fit_points]
    baseline_sd = sample_sd(residuals)
    peak_voltage, peak_height = max(
        ((voltage, current - (slope * voltage + intercept)) for voltage, current in fit_points),
        key=lambda item: item[1],
    )

    return {
        "round": round_no,
        "baseline_slope_nA_per_mV": slope,
        "baseline_intercept_nA": intercept,
        "baseline_residual_sd_nA": baseline_sd,
        "peak_height_nA": peak_height,
        "peak_voltage_mV": peak_voltage,
        "snr": abs(peak_height) / baseline_sd if baseline_sd > 0 else float("inf"),
    }


def plot_sensor(sensor: str,
                curves: list[tuple[int, list[float], list[float]]],
                out_dir: Path) -> list[dict[str, float | int | str]]:
    fig, ax = plt.subplots(figsize=(9.0, 5.4), dpi=160)
    rows: list[dict[str, float | int | str]] = []

    for round_no, voltage_mv, sg5_na in curves:
        metrics = peak_metrics(round_no, voltage_mv, sg5_na)
        baseline = float(metrics["baseline_mean_nA"])
        corrected_na = [current - baseline for current in sg5_na]

        rows.append({"sensor": sensor, **metrics})
        ax.plot(voltage_mv, corrected_na, marker="o", markersize=2.6,
                linewidth=1.4,
                label=(f"Round {round_no}: "
                       f"{float(metrics['peak_height_nA']):.1f} nA "
                       f"@ {float(metrics['peak_voltage_mV']):.1f} mV"))
        ax.scatter([float(metrics["peak_voltage_mV"])],
                   [float(metrics["peak_height_nA"])],
                   s=34, zorder=4)

    ax.axvspan(PEAK_WINDOW_MV[0], PEAK_WINDOW_MV[1],
               color="#8bcf9f", alpha=0.16,
               label=f"peak window {PEAK_WINDOW_MV[0]:.0f}..{PEAK_WINDOW_MV[1]:.0f} mV")
    ax.axhline(0, color="#444444", linewidth=0.8, alpha=0.6)
    ax.set_title(f"PBS blank baseline-subtracted SG5 - sensor {sensor}")
    ax.set_xlabel("E_WE-RE (mV)")
    ax.set_ylabel("Delta_I_sg5 - baseline (nA)")
    ax.grid(True, alpha=0.25)
    ax.legend(loc="best", fontsize=8)
    ax.invert_xaxis()
    fig.tight_layout()

    out_path = out_dir / f"sensor{sensor}_sg5_rounds.png"
    fig.savefig(out_path)
    plt.close(fig)
    print(f"Wrote {out_path}")
    return rows


def plot_sensor_wide_baseline(sensor: str,
                              curves: list[tuple[int, list[float], list[float]]],
                              out_dir: Path) -> list[dict[str, float | int | str]]:
    fig, ax = plt.subplots(figsize=(9.0, 5.4), dpi=160)
    rows: list[dict[str, float | int | str]] = []

    for round_no, voltage_mv, sg5_na in curves:
        metrics = wide_baseline_peak_metrics(round_no, voltage_mv, sg5_na)
        slope = float(metrics["baseline_slope_nA_per_mV"])
        intercept = float(metrics["baseline_intercept_nA"])
        corrected_na = [current - (slope * voltage + intercept)
                        for voltage, current in zip(voltage_mv, sg5_na)]

        rows.append({"sensor": sensor, **metrics})
        ax.plot(voltage_mv, corrected_na, marker="o", markersize=2.6,
                linewidth=1.4,
                label=(f"Round {round_no}: "
                       f"{float(metrics['peak_height_nA']):.1f} nA "
                       f"@ {float(metrics['peak_voltage_mV']):.1f} mV"))
        ax.scatter([float(metrics["peak_voltage_mV"])],
                   [float(metrics["peak_height_nA"])],
                   s=34, zorder=4)

    ax.axvspan(WIDE_BASELINE_WINDOW_MV[0], WIDE_BASELINE_WINDOW_MV[1],
               color="#8bcf9f", alpha=0.16,
               label=(f"fit/find window {WIDE_BASELINE_WINDOW_MV[0]:.0f}.."
                      f"{WIDE_BASELINE_WINDOW_MV[1]:.0f} mV"))
    ax.axhline(0, color="#444444", linewidth=0.8, alpha=0.6)
    ax.set_title(f"PBS blank SG5 after -400..-100 mV linear baseline - sensor {sensor}")
    ax.set_xlabel("E_WE-RE (mV)")
    ax.set_ylabel("Delta_I_sg5 - linear baseline (nA)")
    ax.grid(True, alpha=0.25)
    ax.legend(loc="best", fontsize=8)
    ax.invert_xaxis()
    fig.tight_layout()

    out_path = out_dir / f"sensor{sensor}_sg5_wide_baseline_rounds.png"
    fig.savefig(out_path)
    plt.close(fig)
    print(f"Wrote {out_path}")
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    curves = load_sensor_curves(args.data_dir)
    per_round_rows: list[dict[str, object]] = []
    wide_per_round_rows: list[dict[str, object]] = []

    for sensor, sensor_curves in curves.items():
        if not sensor_curves:
            print(f"WARNING: no curves found for sensor {sensor}")
            continue
        per_round_rows.extend(plot_sensor(sensor, sensor_curves, args.out_dir))
        wide_per_round_rows.extend(plot_sensor_wide_baseline(sensor, sensor_curves, args.out_dir))

    summary_rows: list[dict[str, object]] = []
    for sensor in sorted({str(row["sensor"]) for row in per_round_rows}, key=int):
        sensor_rows = [row for row in per_round_rows if row["sensor"] == sensor]
        heights = [float(row["peak_height_nA"]) for row in sensor_rows]
        voltages = [float(row["peak_voltage_mV"]) for row in sensor_rows]
        sigmas = [float(row["baseline_sd_nA"]) for row in sensor_rows]
        snrs = [float(row["snr"]) for row in sensor_rows]
        peak_sd = sample_sd(heights)
        summary_rows.append({
            "sensor": sensor,
            "n": len(sensor_rows),
            "peak_window_mV": f"{PEAK_WINDOW_MV[0]:.0f}..{PEAK_WINDOW_MV[1]:.0f}",
            "baseline_window_mV": f"{BASELINE_WINDOW_MV[0]:.0f}..{BASELINE_WINDOW_MV[1]:.0f}",
            "peak_height_mean_nA": mean(heights),
            "peak_height_sd_nA": peak_sd,
            "peak_height_cv_pct": (peak_sd / mean(heights) * 100.0) if mean(heights) else 0.0,
            "V_peak_mean_mV": mean(voltages),
            "V_peak_sd_mV": sample_sd(voltages),
            "sigma_blank_mean_nA": mean(sigmas),
            "SNR_mean": mean(snrs),
        })

    write_csv(args.out_dir / "peak_height_per_round.csv", per_round_rows)
    write_csv(args.out_dir / "peak_height_summary.csv", summary_rows)
    write_csv(args.out_dir / "wide_baseline_peak_height_per_round.csv", wide_per_round_rows)

    wide_summary_rows: list[dict[str, object]] = []
    for sensor in sorted({str(row["sensor"]) for row in wide_per_round_rows}, key=int):
        sensor_rows = [row for row in wide_per_round_rows if row["sensor"] == sensor]
        heights = [float(row["peak_height_nA"]) for row in sensor_rows]
        voltages = [float(row["peak_voltage_mV"]) for row in sensor_rows]
        sigmas = [float(row["baseline_residual_sd_nA"]) for row in sensor_rows]
        snrs = [float(row["snr"]) for row in sensor_rows]
        peak_sd = sample_sd(heights)
        wide_summary_rows.append({
            "sensor": sensor,
            "n": len(sensor_rows),
            "fit_and_find_window_mV": (f"{WIDE_BASELINE_WINDOW_MV[0]:.0f}.."
                                       f"{WIDE_BASELINE_WINDOW_MV[1]:.0f}"),
            "peak_height_mean_nA": mean(heights),
            "peak_height_sd_nA": peak_sd,
            "peak_height_cv_pct": (peak_sd / mean(heights) * 100.0) if mean(heights) else 0.0,
            "V_peak_mean_mV": mean(voltages),
            "V_peak_sd_mV": sample_sd(voltages),
            "baseline_residual_sd_mean_nA": mean(sigmas),
            "SNR_mean": mean(snrs),
        })
    write_csv(args.out_dir / "wide_baseline_peak_height_summary.csv", wide_summary_rows)
    print("\nPeak-height summary:")
    for row in summary_rows:
        print(row)
    print("\nWide-baseline peak-height summary:")
    for row in wide_summary_rows:
        print(row)


if __name__ == "__main__":
    main()

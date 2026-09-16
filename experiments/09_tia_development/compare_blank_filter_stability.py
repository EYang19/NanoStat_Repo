#!/usr/bin/env python3
"""Compare PBS blank stability across smoothing filters.

Primary use:
  - later sensor 2/4 blank files in the archived NanoStat session
  - earlier sensor 1/3/5 blank files in the development session

The default peak-height method mirrors the analysis webapp setup:
  - peak search: -300..-200 mV
  - two-shoulder linear baseline: left -400..-330 mV, right -170..-100 mV
"""

from __future__ import annotations

import csv
import glob
import math
import re
from collections import defaultdict
from pathlib import Path
from statistics import mean, stdev


HERE = Path(__file__).resolve().parent
SENSOR_ROOT = HERE.parent / "08_sensor_validation" / "raw_and_processed"
OUT_DIR = HERE / "blank_filter_stability"
PEAK_WINDOW_MV = (-300.0, -200.0)
LEFT_BASELINE_WINDOW_MV = (-400.0, -330.0)
RIGHT_BASELINE_WINDOW_MV = (-170.0, -100.0)

FILTER_COLUMNS = {
    "raw": "Delta_I_nA",
    "avg3": "Delta_I_avg3_nA",
    "smooth5": "Delta_I_smooth5_nA",
    "smooth7": "Delta_I_smooth7_nA",
    "sg5": "Delta_I_sg5_nA",
}

DATASETS = [
    {
        "name": "sensor24_today",
        "glob": str(SENSOR_ROOT / "sensortest819_nanostat" / "sensor24blankpbsrun*.csv"),
        "sensor_by_run_id": {1: "2", 2: "4"},
    },
    {
        "name": "sensor135_previous",
        "glob": str(SENSOR_ROOT / "earlystagenanostatdata" / "pbsblank135run*.csv"),
        "sensor_by_run_id": {1: "1", 2: "3", 3: "5"},
    },
]


def sample_sd(values: list[float]) -> float:
    return stdev(values) if len(values) > 1 else 0.0


def round_index(path: Path) -> int:
    match = re.search(r"run(\d+)", path.stem)
    if match is None:
        return 0
    return int(match.group(1))


def parse_raw_data(path: Path) -> dict[int, list[dict[str, str]]]:
    raw_by_run: dict[int, list[dict[str, str]]] = defaultdict(list)
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
            if not in_raw:
                continue
            if header is None:
                header = row
                continue
            record = dict(zip(header, row))
            raw_by_run[int(record["run_id"])].append(record)

    return raw_by_run


def linear_fit(xs: list[float], ys: list[float]) -> tuple[float, float]:
    x_mean = mean(xs)
    y_mean = mean(ys)
    denom = sum((x - x_mean) ** 2 for x in xs)
    if denom == 0:
        return 0.0, y_mean
    slope = sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) / denom
    intercept = y_mean - slope * x_mean
    return slope, intercept


def metric_for_run(rows: list[dict[str, str]], filter_name: str) -> dict[str, float]:
    col = FILTER_COLUMNS[filter_name]
    points = [(float(r["E_WE_RE_mV"]), float(r[col])) for r in rows]

    baseline_points = [
        (v, i)
        for v, i in points
        if LEFT_BASELINE_WINDOW_MV[0] <= v <= LEFT_BASELINE_WINDOW_MV[1]
        or RIGHT_BASELINE_WINDOW_MV[0] <= v <= RIGHT_BASELINE_WINDOW_MV[1]
    ]
    if len(baseline_points) < 2:
        raise ValueError("not enough baseline points")

    slope, intercept = linear_fit(
        [v for v, _ in baseline_points],
        [i for _, i in baseline_points],
    )
    baseline_residuals = [i - (slope * v + intercept) for v, i in baseline_points]
    sigma_baseline = sample_sd(baseline_residuals)

    peak_candidates = [
        (v, i - (slope * v + intercept))
        for v, i in points
        if PEAK_WINDOW_MV[0] <= v <= PEAK_WINDOW_MV[1]
    ]
    if not peak_candidates:
        raise ValueError("not enough peak points")

    peak_v, peak_height = max(peak_candidates, key=lambda item: item[1])
    snr = abs(peak_height) / sigma_baseline if sigma_baseline else math.inf
    return {
        "peak_height_nA": peak_height,
        "peak_voltage_mV": peak_v,
        "sigma_baseline_nA": sigma_baseline,
        "snr": snr,
        "baseline_slope_nA_per_mV": slope,
    }


def fmt(value: float) -> str:
    if math.isfinite(value):
        return f"{value:.3f}"
    return "inf"


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    per_run_rows: list[dict[str, object]] = []
    summary_rows: list[dict[str, object]] = []

    for dataset in DATASETS:
        paths = sorted((Path(p) for p in glob.glob(str(dataset["glob"]))), key=round_index)
        if not paths:
            print(f"WARNING: no files for {dataset['name']}")
            continue

        for path in paths:
            raw_by_run = parse_raw_data(path)
            for run_id, sensor in dataset["sensor_by_run_id"].items():
                if run_id not in raw_by_run:
                    print(f"WARNING: {path.name} missing run_id {run_id}")
                    continue
                for filter_name in FILTER_COLUMNS:
                    m = metric_for_run(raw_by_run[run_id], filter_name)
                    per_run_rows.append({
                        "dataset": dataset["name"],
                        "file": path.name,
                        "round": round_index(path),
                        "source_run_id": run_id,
                        "sensor": sensor,
                        "filter": filter_name,
                        **m,
                    })

    groups: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in per_run_rows:
        groups[(str(row["dataset"]), str(row["sensor"]), str(row["filter"]))].append(row)

    for (dataset, sensor, filter_name), rows in sorted(groups.items(), key=lambda x: (x[0][0], int(x[0][1]), x[0][2])):
        peaks = [float(r["peak_height_nA"]) for r in rows]
        voltages = [float(r["peak_voltage_mV"]) for r in rows]
        sigmas = [float(r["sigma_baseline_nA"]) for r in rows]
        snrs = [float(r["snr"]) for r in rows]
        peak_mean = mean(peaks)
        peak_sd = sample_sd(peaks)
        summary_rows.append({
            "dataset": dataset,
            "sensor": sensor,
            "filter": filter_name,
            "n_blank": len(rows),
            "peak_window_mV": f"{PEAK_WINDOW_MV[0]:.0f}..{PEAK_WINDOW_MV[1]:.0f}",
            "baseline_left_mV": f"{LEFT_BASELINE_WINDOW_MV[0]:.0f}..{LEFT_BASELINE_WINDOW_MV[1]:.0f}",
            "baseline_right_mV": f"{RIGHT_BASELINE_WINDOW_MV[0]:.0f}..{RIGHT_BASELINE_WINDOW_MV[1]:.0f}",
            "I_peak_mean_nA": peak_mean,
            "I_peak_sd_nA": peak_sd,
            "I_peak_cv_pct": peak_sd / abs(peak_mean) * 100.0 if peak_mean else math.inf,
            "V_peak_mean_mV": mean(voltages),
            "V_peak_sd_mV": sample_sd(voltages),
            "sigma_baseline_mean_nA": mean(sigmas),
            "sigma_baseline_sd_nA": sample_sd(sigmas),
            "SNR_mean": mean(snrs),
        })

    per_run_path = OUT_DIR / "per_run_filter_metrics.csv"
    summary_path = OUT_DIR / "summary_filter_metrics.csv"
    if per_run_rows:
        with per_run_path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(per_run_rows[0].keys()))
            writer.writeheader()
            writer.writerows(per_run_rows)
    if summary_rows:
        with summary_path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(summary_rows[0].keys()))
            writer.writeheader()
            writer.writerows(summary_rows)

    print(f"Wrote {per_run_path}")
    print(f"Wrote {summary_path}")
    print()
    print("Summary:")
    header = (
        "dataset sensor filter n I_mean_nA I_sd_nA CV_pct "
        "V_mean_mV V_sd_mV sigma_mean_nA SNR_mean"
    )
    print(header)
    for row in summary_rows:
        print(
            f"{row['dataset']} {row['sensor']} {row['filter']} {row['n_blank']} "
            f"{fmt(float(row['I_peak_mean_nA']))} {fmt(float(row['I_peak_sd_nA']))} "
            f"{fmt(float(row['I_peak_cv_pct']))} {fmt(float(row['V_peak_mean_mV']))} "
            f"{fmt(float(row['V_peak_sd_mV']))} {fmt(float(row['sigma_baseline_mean_nA']))} "
            f"{fmt(float(row['SNR_mean']))}"
        )


if __name__ == "__main__":
    main()

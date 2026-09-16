#!/usr/bin/env python3
"""Analyze NanoStat PBS repeated SWV runs by physical sensor.

Default input mapping matches raw_and_processed/earlystagenanostatdata from
the 2026-08-05 session:
  run1: sensors 2, 4, 6
  run2/run3: sensors 4, 6
  run4: sensor 4 failure export plus sensor 6
  run5: sensor 6
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, stdev


DEFAULT_DATA_DIR = (
    Path(__file__).resolve().parent
    / "raw_and_processed"
    / "earlystagenanostatdata"
)

# Physical mapping for the current PBS dataset. Each item is:
# (filename, csv run_id or None for single-run export, physical sensor, experiment index).
RUN_MAP = [
    ("pbs246run1.csv", 1, "2", 1),
    ("pbs246run1.csv", 2, "4", 1),
    ("pbs246run1.csv", 3, "6", 1),
    ("pbs46run2.csv", 5, "4", 2),
    ("pbs46run2.csv", 6, "6", 2),
    ("pbs46run3.csv", 1, "4", 3),
    ("pbs46run3.csv", 2, "6", 3),
    ("nanostat_swv_120Hz_2026-08-05T12-10-50-414Z.csv", None, "4", 4),
    ("pbs6run4.csv", 2, "6", 4),
    ("pbs6run5.csv", 1, "6", 5),
]

# Chosen by observed PBS/MB-like peak region for each sensor. Sensor 2 only has
# one usable run in this folder, with a peak centered farther left.
PEAK_WINDOWS_MV = {
    "2": (-300.0, -220.0),
    "4": (-260.0, -190.0),
    "6": (-260.0, -190.0),
}

BASELINE_WINDOWS_MV = {
    "2": (-450.0, -320.0),
    "4": (-450.0, -320.0),
    "6": (-450.0, -320.0),
}


@dataclass
class RunData:
    file: str
    source_run_id: int | None
    sensor: str
    experiment: int
    timestamp: str
    frequency_hz: int
    tia_path: str
    rows: list[dict[str, str]]


@dataclass
class Metrics:
    sensor: str
    experiment: int
    file: str
    source_run_id: int | None
    timestamp: str
    frequency_hz: int
    tia_path: str
    peak_window: tuple[float, float]
    baseline_window: tuple[float, float]
    i_peak_nA: float
    v_peak_mV: float
    sigma_blank_nA: float
    snr: float
    points_in_peak_window: int
    points_in_baseline_window: int
    status: str


def sample_sd(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    return stdev(values)


def parse_csv(path: Path) -> tuple[dict[str, str], dict[int, dict[str, str]], dict[int, list[dict[str, str]]], list[dict[str, str]]]:
    meta: dict[str, str] = {}
    summaries: dict[int, dict[str, str]] = {}
    raw_by_run: dict[int, list[dict[str, str]]] = {}
    single_rows: list[dict[str, str]] = []
    section = "single"
    header: list[str] | None = None

    with path.open(newline="") as handle:
        for row in csv.reader(handle):
            if not row:
                continue
            if row[0].startswith("#"):
                key = row[0][2:] if row[0].startswith("# ") else row[0][1:]
                if len(row) > 1:
                    meta[key] = row[1]
                continue
            if row[0] == "SUMMARY":
                section = "summary"
                header = None
                continue
            if row[0] == "RAW_DATA":
                section = "raw"
                header = None
                continue
            if header is None:
                header = row
                continue

            rec = dict(zip(header, row))
            if section == "summary":
                summaries[int(rec["run_id"])] = rec
            elif section == "raw":
                raw_by_run.setdefault(int(rec["run_id"]), []).append(rec)
            else:
                single_rows.append(rec)

    return meta, summaries, raw_by_run, single_rows


def load_runs(data_dir: Path) -> list[RunData]:
    parsed_cache = {}
    runs: list[RunData] = []

    for filename, source_run_id, sensor, experiment in RUN_MAP:
        path = data_dir / filename
        if path not in parsed_cache:
            parsed_cache[path] = parse_csv(path)
        meta, summaries, raw_by_run, single_rows = parsed_cache[path]

        if source_run_id is None:
            rows = single_rows
            timestamp = meta.get("Exported_at", "")
            frequency_hz = int(float(meta.get("Frequency_Hz", "0")))
            tia_path = meta.get("TIA_path", "")
        else:
            rows = raw_by_run[source_run_id]
            summary = summaries.get(source_run_id, {})
            timestamp = summary.get("timestamp", "")
            frequency_hz = int(float(summary.get("frequency_hz", "0")))
            tia_path = summary.get("tia_path", "")

        runs.append(RunData(filename, source_run_id, sensor, experiment,
                            timestamp, frequency_hz, tia_path, rows))

    return runs


def compute_metrics(run: RunData) -> Metrics:
    peak_window = PEAK_WINDOWS_MV[run.sensor]
    baseline_window = BASELINE_WINDOWS_MV[run.sensor]
    peak_points: list[tuple[float, float]] = []
    baseline_values: list[float] = []

    for row in run.rows:
        voltage = float(row["E_WE_RE_mV"])
        current = float(row["Delta_I_sg5_nA"])
        if peak_window[0] <= voltage <= peak_window[1]:
            peak_points.append((voltage, current))
        if baseline_window[0] <= voltage <= baseline_window[1]:
            baseline_values.append(current)

    if not peak_points:
        raise ValueError(f"No points in peak window for {run.file} sensor {run.sensor}")

    v_peak, i_peak = max(peak_points, key=lambda item: item[1])
    sigma_blank = sample_sd(baseline_values)
    snr = abs(i_peak) / sigma_blank if sigma_blank > 0 else math.inf

    status = "ok"
    if abs(i_peak) >= 2500.0 or sigma_blank >= 300.0:
        status = "failed_or_saturated"

    return Metrics(run.sensor, run.experiment, run.file, run.source_run_id,
                   run.timestamp, run.frequency_hz, run.tia_path,
                   peak_window, baseline_window, i_peak, v_peak,
                   sigma_blank, snr, len(peak_points), len(baseline_values),
                   status)


def summarize(metrics: list[Metrics], include_failed: bool) -> list[dict[str, object]]:
    rows = []
    sensors = sorted({m.sensor for m in metrics}, key=int)

    for sensor in sensors:
        selected = [m for m in metrics if m.sensor == sensor]
        if not include_failed:
            selected = [m for m in selected if m.status == "ok"]

        if not selected:
            continue

        i_values = [m.i_peak_nA for m in selected]
        v_values = [m.v_peak_mV for m in selected]
        sigma_values = [m.sigma_blank_nA for m in selected]
        snr_values = [m.snr for m in selected]
        rows.append({
            "sensor": sensor,
            "n": len(selected),
            "experiments": ",".join(str(m.experiment) for m in selected),
            "peak_window_mV": f"{PEAK_WINDOWS_MV[sensor][0]:.0f}..{PEAK_WINDOWS_MV[sensor][1]:.0f}",
            "I_peak_mean_nA": mean(i_values),
            "I_peak_sd_nA": sample_sd(i_values),
            "V_peak_mean_mV": mean(v_values),
            "V_peak_sd_mV": sample_sd(v_values),
            "sigma_blank_nA": mean(sigma_values),
            "SNR_mean": mean(snr_values),
        })

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
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "analysis_outputs" / "pbstest_analysis",
    )
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    metrics = [compute_metrics(run) for run in load_runs(args.data_dir)]

    per_run_rows = [
        {
            "sensor": m.sensor,
            "experiment": m.experiment,
            "status": m.status,
            "file": m.file,
            "source_run_id": "" if m.source_run_id is None else m.source_run_id,
            "timestamp": m.timestamp,
            "frequency_hz": m.frequency_hz,
            "tia_path": m.tia_path,
            "peak_window_mV": f"{m.peak_window[0]:.0f}..{m.peak_window[1]:.0f}",
            "baseline_window_mV": f"{m.baseline_window[0]:.0f}..{m.baseline_window[1]:.0f}",
            "I_peak_nA": m.i_peak_nA,
            "V_peak_mV": m.v_peak_mV,
            "sigma_blank_nA": m.sigma_blank_nA,
            "SNR": m.snr,
            "points_peak": m.points_in_peak_window,
            "points_baseline": m.points_in_baseline_window,
        }
        for m in metrics
    ]
    ok_summary = summarize(metrics, include_failed=False)
    all_summary = summarize(metrics, include_failed=True)

    write_csv(args.out_dir / "per_run_metrics.csv", per_run_rows)
    write_csv(args.out_dir / "sensor_summary_ok_only.csv", ok_summary)
    write_csv(args.out_dir / "sensor_summary_all_runs.csv", all_summary)

    print("Per-run metrics:")
    for row in per_run_rows:
        print(row)
    print("\nSensor summary, ok runs only:")
    for row in ok_summary:
        print(row)
    print("\nSensor summary, all mapped runs:")
    for row in all_summary:
        print(row)
    print(f"\nWrote CSV outputs to {args.out_dir}")


if __name__ == "__main__":
    main()

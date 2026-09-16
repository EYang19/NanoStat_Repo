#!/usr/bin/env python3
"""Plot cortisol concentration response curves from NanoStat and Autolab files.

The analysis mirrors the browser analysis app:
  - SG5 smoothing, using the exported NanoStat SG5 column or the same 5-point
    Savitzky-Golay kernel for NOVA txt files.
  - auto two-shoulder linear baseline.
  - peak height is the maximum baseline-subtracted SG5 current in -300..-200 mV.
  - percent change is relative to the mean blank peak height for each sensor.
"""

from __future__ import annotations

import csv
import math
import re
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, stdev

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


HERE = Path(__file__).resolve().parent
DATA_ROOT = HERE / "raw_and_processed"
OUT_DIR = HERE / "analysis_outputs" / "cortisol_change_curves"

PEAK_WINDOW_MV = (-300.0, -200.0)
LEFT_BASELINE_MV = (-400.0, -330.0)
RIGHT_BASELINE_MV = (-170.0, -100.0)

DATASETS = {
    "nanostat_2026-08-19_sensors2456": {
        "title": "19 Aug 2026 NanoStat PBS response",
        "patterns": [
            "sensortest819_nanostat/sensor24blankpbsrun*.csv",
            "sensortest819_nanostat/sensor24pbs*ng.csv",
            "sensortest819_nanostat/sensor56blankpbsrun*.csv",
            "sensortest819_nanostat/sensor56pbs*ng.csv",
        ],
        "instrument": "NanoStat",
    },
    "autolab_2026-08-20_sensors1246": {
        "title": "20 Aug 2026 Autolab PBS response",
        "patterns": ["sensortest820_autolab/sensor*pbs*.txt"],
        "instrument": "Autolab",
    },
    "nanostat_135_development": {
        "title": "Earlier Sensor 1/3/5 NanoStat PBS response",
        "patterns": [
            "earlystagenanostatdata/pbsblank135run*.csv",
            "earlystagenanostatdata/pbs*ng135.csv",
        ],
        "instrument": "NanoStat",
    },
}

SENSOR_COLORS = {
    "1": "#1f6feb",
    "2": "#d24b35",
    "3": "#16833a",
    "4": "#8a4bd3",
    "5": "#b87514",
    "6": "#0f7d80",
}


@dataclass
class Run:
    dataset: str
    instrument: str
    file_name: str
    run_id: str
    sensor: str
    kind: str
    concentration: float
    note: str
    voltage_mv: list[float]
    current_sg5_na: list[float]


@dataclass
class Metric:
    dataset: str
    instrument: str
    file_name: str
    run_id: str
    sensor: str
    kind: str
    concentration: float
    note: str
    peak_height_nA: float
    peak_voltage_mV: float
    baseline_sigma_nA: float
    blank_mean_nA: float | None = None
    change_pct: float | None = None


def parse_csv_line(line: str) -> list[str]:
    return next(csv.reader([line]))


def smooth_at(values: list[float], index: int, radius: int) -> float:
    pts = [values[i] for i in range(index - radius, index + radius + 1)
           if 0 <= i < len(values) and math.isfinite(values[i])]
    return mean(pts) if pts else math.nan


def sg5_at(values: list[float], index: int) -> float:
    if index < 2 or index + 2 >= len(values):
        return smooth_at(values, index, 1)
    return (
        -3 * values[index - 2]
        + 12 * values[index - 1]
        + 17 * values[index]
        + 12 * values[index + 1]
        - 3 * values[index + 2]
    ) / 35


def add_sg5(values: list[float]) -> list[float]:
    return [sg5_at(values, i) for i in range(len(values))]


def sample_sd(values: list[float]) -> float:
    return stdev(values) if len(values) >= 2 else 0.0


def lin_fit(points: list[tuple[float, float]]) -> tuple[float, float]:
    if not points:
        return 0.0, 0.0
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    mx = mean(xs)
    my = mean(ys)
    den = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in points) / den if den else 0.0
    return slope, my - slope * mx


def in_window(x: float, window: tuple[float, float]) -> bool:
    return window[0] <= x <= window[1]


def infer_pair_sensor(file_name: str, run_id: str, run_ordinal: int) -> tuple[str, str]:
    lower = file_name.lower()
    numeric_run = int(run_id) if str(run_id).isdigit() else run_ordinal
    note = ""

    if "sensor24" in lower:
        mapping = {1: "2", 2: "4"}
        return (mapping.get(numeric_run) or mapping.get(run_ordinal) or str(numeric_run), note)
    if "sensor56" in lower:
        mapping = {1: "5", 2: "6"}
        return (mapping.get(numeric_run) or mapping.get(run_ordinal) or str(numeric_run), note)
    if "135" in lower:
        if "10000ng" in lower and numeric_run >= 4:
            return "5", "sensor 5 repeat check at 10000 ng/mL"
        mapping = {1: "1", 2: "3", 3: "5"}
        return (mapping.get(numeric_run) or mapping.get(run_ordinal) or str(numeric_run), note)

    explicit = re.search(r"sensor\s*(\d+)", lower)
    if explicit:
        return explicit.group(1), note
    return str(numeric_run), note


def infer_kind_and_conc(file_name: str) -> tuple[str, float]:
    lower = file_name.lower()
    if "blank" in lower:
        return "blank", 0.0
    match = re.search(r"pbs(\d+(?:\.\d+)?)\s*ng", lower)
    if match:
        return "calibration", float(match.group(1))
    return "sample", math.nan


def parse_nanostat_csv(path: Path, dataset: str, instrument: str) -> list[Run]:
    text = path.read_text(errors="replace")
    lines = [line for line in text.replace("\r", "").split("\n") if line]
    meta: dict[str, str] = {}
    summary: dict[str, dict[str, str]] = {}
    raw: dict[str, list[dict[str, str]]] = {}
    single: list[dict[str, str]] = []
    section = "single"
    header: list[str] | None = None

    for line in lines:
        if line.startswith("#"):
            cols = parse_csv_line(re.sub(r"^#\s?", "", line))
            if len(cols) > 1:
                meta[cols[0]] = cols[1]
            continue
        if line == "SUMMARY":
            section = "summary"
            header = None
            continue
        if line == "RAW_DATA":
            section = "raw"
            header = None
            continue

        cols = parse_csv_line(line)
        if header is None:
            header = cols
            continue
        row = dict(zip(header, cols))
        if section == "summary":
            summary[row["run_id"]] = row
        elif section == "raw":
            raw.setdefault(row["run_id"], []).append(row)
        else:
            single.append(row)

    out: list[Run] = []
    kind, inferred_conc = infer_kind_and_conc(path.name)
    groups = raw if raw else {"1": single}
    for ordinal, (run_id, rows) in enumerate(groups.items(), start=1):
        sensor, note = infer_pair_sensor(path.name, run_id, ordinal)
        conc = inferred_conc
        if kind != "blank":
            from_summary = float(summary.get(run_id, {}).get("concentration", "nan"))
            conc = from_summary if math.isfinite(from_summary) and from_summary > 0 else inferred_conc

        voltage = [float(r["E_WE_RE_mV"]) for r in rows]
        if "Delta_I_sg5_nA" in rows[0]:
            current = [float(r["Delta_I_sg5_nA"]) for r in rows]
        else:
            current = add_sg5([float(r["Delta_I_nA"]) for r in rows])

        out.append(Run(dataset, instrument, path.name, run_id, sensor, kind, conc, note, voltage, current))

    return out


def parse_nova_txt(path: Path, dataset: str, instrument: str) -> list[Run]:
    lines = [line for line in path.read_text(errors="replace").replace("\r", "").split("\n") if line]
    header = [h.strip() for h in lines[0].split(";")]
    lower = [h.lower() for h in header]
    potential_idx = next((i for i, h in enumerate(lower) if h == "potential applied (v)" or "potential applied" in h), -1)
    delta_idx = next((i for i, h in enumerate(lower) if "δ.current" in h or "delta.current" in h), -1)
    if delta_idx < 0:
        delta_idx = next((i for i, h in enumerate(lower)
                          if "current (a)" in h and "forward" not in h and "backward" not in h), -1)
    if potential_idx < 0 or delta_idx < 0:
        return []

    voltage: list[float] = []
    raw_current: list[float] = []
    for line in lines[1:]:
        cols = [c.strip() for c in line.split(";")]
        try:
            voltage.append(float(cols[potential_idx]) * 1000.0)
            raw_current.append(float(cols[delta_idx]) * 1e9)
        except (ValueError, IndexError):
            continue

    sensor, note = infer_pair_sensor(path.name, "1", 1)
    kind, conc = infer_kind_and_conc(path.name)
    return [Run(dataset, instrument, path.name, "1", sensor, kind, conc, note, voltage, add_sg5(raw_current))]


def compute_metric(run: Run) -> Metric:
    pts = list(zip(run.voltage_mv, run.current_sg5_na))
    baseline_pts = [
        (x, y) for x, y in pts
        if in_window(x, LEFT_BASELINE_MV) or in_window(x, RIGHT_BASELINE_MV)
    ]
    slope, intercept = lin_fit(baseline_pts)
    peak_pts = [
        (x, y - (slope * x + intercept)) for x, y in pts
        if in_window(x, PEAK_WINDOW_MV)
    ]
    if not peak_pts:
        raise ValueError(f"No peak points in {run.file_name} run {run.run_id}")
    peak_v, peak_h = max(peak_pts, key=lambda item: item[1])
    residuals = [y - (slope * x + intercept) for x, y in baseline_pts]
    return Metric(
        dataset=run.dataset,
        instrument=run.instrument,
        file_name=run.file_name,
        run_id=run.run_id,
        sensor=run.sensor,
        kind=run.kind,
        concentration=run.concentration,
        note=run.note,
        peak_height_nA=peak_h,
        peak_voltage_mV=peak_v,
        baseline_sigma_nA=sample_sd(residuals),
    )


def collect_runs() -> list[Run]:
    all_runs: list[Run] = []
    for dataset, cfg in DATASETS.items():
        for pattern in cfg["patterns"]:
            for path in sorted(DATA_ROOT.glob(pattern)):
                if path.suffix.lower() == ".txt":
                    all_runs.extend(parse_nova_txt(path, dataset, cfg["instrument"]))
                else:
                    all_runs.extend(parse_nanostat_csv(path, dataset, cfg["instrument"]))
    return all_runs


def add_percent_changes(metrics: list[Metric]) -> None:
    by_key: dict[tuple[str, str], list[Metric]] = {}
    for metric in metrics:
        by_key.setdefault((metric.dataset, metric.sensor), []).append(metric)

    for group in by_key.values():
        blanks = [m.peak_height_nA for m in group if m.kind == "blank"]
        if not blanks:
            continue
        blank_mean = mean(blanks)
        for metric in group:
            metric.blank_mean_nA = blank_mean
            metric.change_pct = (metric.peak_height_nA - blank_mean) / abs(blank_mean) * 100.0


def aggregate_response(metrics: list[Metric]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    keys = sorted(
        {
            (m.dataset, m.instrument, m.sensor, m.concentration)
            for m in metrics
            if m.kind == "calibration" and m.change_pct is not None and math.isfinite(m.concentration)
        },
        key=lambda x: (x[0], int(x[2]), x[3]),
    )
    for dataset, instrument, sensor, conc in keys:
        group = [
            m for m in metrics
            if m.dataset == dataset and m.sensor == sensor and m.concentration == conc
            and m.kind == "calibration" and m.change_pct is not None
        ]
        changes = [m.change_pct for m in group if m.change_pct is not None]
        peaks = [m.peak_height_nA for m in group]
        rows.append({
            "dataset": dataset,
            "instrument": instrument,
            "sensor": sensor,
            "concentration_ng_mL": conc,
            "n": len(group),
            "change_pct_mean": mean(changes),
            "change_pct_sd": sample_sd(changes),
            "peak_height_nA_mean": mean(peaks),
            "peak_height_nA_sd": sample_sd(peaks),
            "blank_mean_nA": group[0].blank_mean_nA,
        })
    return rows


def first_threshold_crossing(points: list[tuple[float, float]],
                             threshold_pct: float) -> tuple[str, float | None, str]:
    """Return crossing status, LOD concentration, and a short bracket label.

    Points are (concentration, response percent) sorted by concentration. The
    crossing is interpolated linearly on log10(concentration), matching the
    log-spaced response-curve view used for the PBS plots.
    """
    if not points or not math.isfinite(threshold_pct):
        return "invalid", None, ""
    points = sorted(points, key=lambda p: p[0])
    for index, (conc, response) in enumerate(points):
        if response < threshold_pct:
            continue
        if index == 0:
            return "below_first_tested_point", conc, f"<= {conc:g}"
        prev_conc, prev_response = points[index - 1]
        if response == prev_response:
            return "step_crossing", conc, f"{prev_conc:g}-{conc:g}"
        frac = (threshold_pct - prev_response) / (response - prev_response)
        log_lod = math.log10(prev_conc) + max(0.0, min(1.0, frac)) * (
            math.log10(conc) - math.log10(prev_conc)
        )
        return "interpolated", 10 ** log_lod, f"{prev_conc:g}-{conc:g}"
    return "above_tested_range", None, f"> {points[-1][0]:g}"


def functional_lod_summary(metrics: list[Metric],
                           response_rows: list[dict[str, object]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    keys = sorted(
        {(m.dataset, m.instrument, m.sensor) for m in metrics},
        key=lambda item: (item[0], int(item[2])),
    )
    for dataset, instrument, sensor in keys:
        group = [m for m in metrics if m.dataset == dataset and m.sensor == sensor]
        blanks = [m.peak_height_nA for m in group if m.kind == "blank"]
        if len(blanks) < 2:
            continue
        blank_mean = mean(blanks)
        blank_sd = sample_sd(blanks)
        sigma_blank_pct = blank_sd / abs(blank_mean) * 100.0 if blank_mean else math.nan
        threshold_pct = 3.0 * sigma_blank_pct
        response_points = sorted([
            (float(r["concentration_ng_mL"]), float(r["change_pct_mean"]))
            for r in response_rows
            if r["dataset"] == dataset and str(r["sensor"]) == sensor
        ], key=lambda item: item[0])
        status, lod, bracket = first_threshold_crossing(response_points, threshold_pct)

        log_points = [
            (math.log10(m.concentration), m.change_pct)
            for m in group
            if m.kind == "calibration" and m.change_pct is not None and m.concentration > 0
        ]
        pct_slope, pct_intercept = lin_fit(log_points)
        old_pct_lod = (
            10 ** ((threshold_pct - pct_intercept) / pct_slope)
            if pct_slope else math.nan
        )

        abs_points = [
            (m.concentration, m.peak_height_nA - blank_mean)
            for m in group
            if m.kind == "calibration" and math.isfinite(m.concentration)
        ]
        abs_slope, _ = lin_fit(abs_points)
        old_abs_lod = 3.0 * blank_sd / abs(abs_slope) if abs_slope else math.nan

        monotonic = all(
            response_points[i][1] >= response_points[i - 1][1]
            for i in range(1, len(response_points))
        )
        rows.append({
            "dataset": dataset,
            "instrument": instrument,
            "sensor": sensor,
            "blank_n": len(blanks),
            "blank_mean_nA": blank_mean,
            "blank_sd_nA": blank_sd,
            "sigma_blank_pct": sigma_blank_pct,
            "functional_threshold_pct": threshold_pct,
            "functional_lod_ng_mL": lod if lod is not None else "",
            "functional_status": status,
            "functional_bracket_ng_mL": bracket,
            "response_monotonic": "yes" if monotonic else "no",
            "old_percent_log_fit_lod_ng_mL": old_pct_lod,
            "old_percent_log_fit_slope_pct_per_decade": pct_slope,
            "old_absolute_linear_lod_ng_mL": old_abs_lod,
            "old_absolute_linear_slope_nA_per_ng_mL": abs_slope,
        })
    return rows


def write_dicts(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def plot_dataset(dataset: str, metrics: list[Metric], response_rows: list[dict[str, object]]) -> Path:
    cfg = DATASETS[dataset]
    rows = [r for r in response_rows if r["dataset"] == dataset]
    sensors = sorted({str(r["sensor"]) for r in rows}, key=int)

    fig, ax = plt.subplots(figsize=(8.8, 5.4), dpi=180)
    colors = {
        "1": "#1f6feb",
        "2": "#d24b35",
        "3": "#16833a",
        "4": "#8a4bd3",
        "5": "#b87514",
        "6": "#0f7d80",
    }

    for sensor in sensors:
        sensor_rows = sorted([r for r in rows if str(r["sensor"]) == sensor],
                             key=lambda r: float(r["concentration_ng_mL"]))
        x = [float(r["concentration_ng_mL"]) for r in sensor_rows]
        y = [float(r["change_pct_mean"]) for r in sensor_rows]
        yerr = [float(r["change_pct_sd"]) if int(r["n"]) > 1 else 0.0 for r in sensor_rows]
        ax.errorbar(
            x,
            y,
            yerr=yerr,
            marker="o",
            linewidth=1.8,
            markersize=5,
            capsize=3,
            color=colors.get(sensor, None),
            label=f"Sensor {sensor}",
        )

        raw_points = [
            m for m in metrics
            if m.dataset == dataset and m.sensor == sensor and m.kind == "calibration"
            and m.change_pct is not None
        ]
        if raw_points:
            ax.scatter(
                [m.concentration for m in raw_points],
                [m.change_pct for m in raw_points],
                s=18,
                color=colors.get(sensor, None),
                alpha=0.35,
                zorder=3,
            )

    ax.axhline(0, color="#555", linewidth=0.8, alpha=0.7)
    ax.set_xscale("log")
    ax.set_xlabel("Cortisol concentration (ng/mL)")
    ax.set_ylabel("Change in peak current (%)")
    ax.set_title(cfg["title"])
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(loc="best", fontsize=8)
    ax.text(
        0.01,
        0.02,
        "SG5; auto two-shoulder baseline; peak -300..-200 mV; left -400..-330 mV; right -170..-100 mV",
        transform=ax.transAxes,
        fontsize=7.5,
        color="#555",
        va="bottom",
    )
    fig.tight_layout()
    out = OUT_DIR / f"{dataset}_change_current_sg5.png"
    fig.savefig(out)
    plt.close(fig)
    return out


def plot_combined(response_rows: list[dict[str, object]]) -> Path:
    fig, axes = plt.subplots(1, 3, figsize=(15.6, 4.8), dpi=180, sharey=False)
    for ax, dataset in zip(axes, DATASETS):
        rows = [r for r in response_rows if r["dataset"] == dataset]
        sensors = sorted({str(r["sensor"]) for r in rows}, key=int)
        for sensor in sensors:
            sensor_rows = sorted([r for r in rows if str(r["sensor"]) == sensor],
                                 key=lambda r: float(r["concentration_ng_mL"]))
            ax.plot(
                [float(r["concentration_ng_mL"]) for r in sensor_rows],
                [float(r["change_pct_mean"]) for r in sensor_rows],
                marker="o",
                linewidth=1.4,
                label=f"S{sensor}",
            )
        ax.axhline(0, color="#555", linewidth=0.8, alpha=0.7)
        ax.set_xscale("log")
        ax.set_title(DATASETS[dataset]["title"], fontsize=10)
        ax.set_xlabel("ng/mL")
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(fontsize=7)
    axes[0].set_ylabel("Change in peak current (%)")
    fig.suptitle("PBS cortisol response from SG5 peak extraction", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    out = OUT_DIR / "combined_change_current_sg5.png"
    fig.savefig(out)
    plt.close(fig)
    return out


def plot_functional_lod(response_rows: list[dict[str, object]],
                        lod_rows: list[dict[str, object]]) -> Path:
    fig, axes = plt.subplots(1, 3, figsize=(15.6, 4.8), dpi=180, sharey=False)
    for ax, dataset in zip(axes, DATASETS):
        rows = [r for r in response_rows if r["dataset"] == dataset]
        summaries = {str(r["sensor"]): r for r in lod_rows if r["dataset"] == dataset}
        sensors = sorted({str(r["sensor"]) for r in rows}, key=int)
        for sensor in sensors:
            sensor_rows = sorted([r for r in rows if str(r["sensor"]) == sensor],
                                 key=lambda r: float(r["concentration_ng_mL"]))
            color = SENSOR_COLORS.get(sensor, None)
            x = [float(r["concentration_ng_mL"]) for r in sensor_rows]
            y = [float(r["change_pct_mean"]) for r in sensor_rows]
            ax.plot(x, y, marker="o", linewidth=1.5, color=color, label=f"S{sensor}")
            summary = summaries.get(sensor)
            if summary:
                threshold = float(summary["functional_threshold_pct"])
                ax.axhline(threshold, color=color, linestyle="--", linewidth=0.9, alpha=0.35)
                lod = summary["functional_lod_ng_mL"]
                if lod != "":
                    ax.axvline(float(lod), color=color, linestyle=":", linewidth=0.9, alpha=0.35)
        ax.axhline(0, color="#555", linewidth=0.8, alpha=0.7)
        ax.set_xscale("log")
        ax.set_title(DATASETS[dataset]["title"], fontsize=10)
        ax.set_xlabel("ng/mL")
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(fontsize=7)
    axes[0].set_ylabel("Change in peak current (%)")
    fig.suptitle("Functional LOD thresholds from SG5 peak extraction", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    out = OUT_DIR / "functional_lod_thresholds_sg5.png"
    fig.savefig(out)
    plt.close(fig)
    return out


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    runs = collect_runs()
    metrics = [compute_metric(run) for run in runs]
    add_percent_changes(metrics)
    response_rows = aggregate_response(metrics)
    lod_rows = functional_lod_summary(metrics, response_rows)

    per_run_rows = [
        {
            "dataset": m.dataset,
            "instrument": m.instrument,
            "file_name": m.file_name,
            "run_id": m.run_id,
            "sensor": m.sensor,
            "kind": m.kind,
            "concentration_ng_mL": m.concentration,
            "note": m.note,
            "peak_height_nA": m.peak_height_nA,
            "peak_voltage_mV": m.peak_voltage_mV,
            "baseline_sigma_nA": m.baseline_sigma_nA,
            "blank_mean_nA": m.blank_mean_nA,
            "change_pct": m.change_pct,
        }
        for m in metrics
    ]
    write_dicts(OUT_DIR / "per_run_peak_metrics.csv", per_run_rows)
    write_dicts(OUT_DIR / "concentration_response_points.csv", response_rows)
    write_dicts(OUT_DIR / "functional_lod_summary.csv", lod_rows)

    paths = [plot_dataset(dataset, metrics, response_rows) for dataset in DATASETS]
    paths.append(plot_combined(response_rows))
    paths.append(plot_functional_lod(response_rows, lod_rows))

    print(f"Parsed {len(runs)} runs.")
    for path in paths:
        print(path)
    print(OUT_DIR / "per_run_peak_metrics.csv")
    print(OUT_DIR / "concentration_response_points.csv")
    print(OUT_DIR / "functional_lod_summary.csv")


if __name__ == "__main__":
    main()

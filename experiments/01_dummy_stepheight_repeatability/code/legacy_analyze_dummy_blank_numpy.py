#!/usr/bin/env python3
"""Analyze NanoStat dummy-cell blank exports without pandas.

The input files are NanoStat multi-run CSV exports with SUMMARY and RAW_DATA
sections. The script writes summary CSV files, PNG figures, and a short report.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
from pathlib import Path

# Keep Matplotlib's font/config cache out of the read-only user home in the
# managed environment. This also makes the script reproducible on clean hosts.
MPL_CACHE = Path("/private/tmp/nanostat-matplotlib")
MPL_CACHE.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MPL_CACHE))
os.environ.setdefault("MPLBACKEND", "Agg")

import matplotlib.pyplot as plt
import numpy as np


PROFILE_ORDER = ["5MV", "2MV", "1MVSEG"]
PROFILE_LABEL = {"5MV": "5 mV", "2MV": "2 mV", "1MVSEG": "1 mV segmented"}
PROFILE_COLOR = {"5MV": "#1769aa", "2MV": "#d97706", "1MVSEG": "#16803c"}
EXPECTED_NA = 70.187


def number(value: str | None) -> float:
    if value is None or value.strip() in {"", "NaN", "nan", "None"}:
        return float("nan")
    try:
        return float(value)
    except ValueError:
        return float("nan")


def read_export(path: Path) -> tuple[list[dict], list[dict], dict[str, str]]:
    """Read metadata, SUMMARY rows, and RAW_DATA rows from one export."""
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    metadata: dict[str, str] = {}
    for line in lines:
        if line.startswith("# "):
            key, _, value = line[2:].partition(",")
            metadata[key.strip()] = value.strip()

    def read_section(name: str, next_name: str | None) -> list[dict]:
        try:
            start = lines.index(name) + 1
        except ValueError as exc:
            raise ValueError(f"{path}: missing {name} section") from exc
        end = len(lines)
        if next_name and next_name in lines:
            end = lines.index(next_name)
        rows = list(csv.DictReader(lines[start:end]))
        return [row for row in rows if any(str(v).strip() for v in row.values())]

    return read_section("SUMMARY", "RAW_DATA"), read_section("RAW_DATA", None), metadata


def load_data(input_dir: Path) -> tuple[list[dict], list[dict], list[dict]]:
    summaries: list[dict] = []
    raw: list[dict] = []
    files: list[dict] = []
    paths = sorted(input_dir.glob("*.csv"))
    if not paths:
        raise FileNotFoundError(f"No CSV files found in {input_dir}")
    for path in paths:
        summary_rows, raw_rows, metadata = read_export(path)
        if not summary_rows:
            raise ValueError(f"{path}: SUMMARY section is empty")
        profile = summary_rows[0].get("dummy_profile", "").upper()
        for row in summary_rows:
            row["profile"] = profile
            row["source_file"] = path.name
            summaries.append(row)
        for row in raw_rows:
            row["profile"] = profile
            row["source_file"] = path.name
            raw.append(row)
        files.append({"source_file": path.name, "profile": profile, **metadata})
    return summaries, raw, files


def finite(values: list[float] | np.ndarray) -> np.ndarray:
    values = np.asarray(values, dtype=float)
    return values[np.isfinite(values)]


def summary_stats(rows: list[dict]) -> list[dict]:
    output = []
    for profile in PROFILE_ORDER:
        selected = [r for r in rows if r["profile"] == profile]
        if not selected:
            continue
        means = np.array([number(r.get("dummy_mean_delta_nA")) for r in selected])
        sds = np.array([number(r.get("dummy_sd_delta_nA")) for r in selected])
        errors = np.array([number(r.get("dummy_error_pct")) for r in selected])
        durations = np.array([number(r.get("dummy_scan_ms")) for r in selected])
        expected = finite([number(r.get("dummy_expected_delta_nA")) for r in selected])
        expected_value = float(np.nanmean(expected)) if expected.size else EXPECTED_NA
        measured_mean = float(np.nanmean(means))
        run_sd = float(np.nanstd(means, ddof=1)) if len(finite(means)) > 1 else float("nan")
        output.append({
            "profile": profile,
            "label": PROFILE_LABEL[profile],
            "runs": len(selected),
            "mean_delta_nA": measured_mean,
            "run_to_run_sd_nA": run_sd,
            "run_to_run_cv_pct": 100 * run_sd / measured_mean,
            "min_delta_nA": float(np.nanmin(means)),
            "max_delta_nA": float(np.nanmax(means)),
            "expected_delta_nA": expected_value,
            "bias_nA": measured_mean - expected_value,
            "bias_pct": 100 * (measured_mean - expected_value) / expected_value,
            "mean_error_pct": float(np.nanmean(errors)),
            "mean_scan_s": float(np.nanmean(durations)) / 1000,
            "mean_within_scan_sd_nA": float(np.nanmean(sds)),
        })
    return output


def raw_arrays(rows: list[dict], profile: str, column: str = "Delta_I_nA"):
    selected = [r for r in rows if r["profile"] == profile]
    runs = sorted({int(r["run_id"]) for r in selected})
    traces = []
    for run_id in runs:
        run = [r for r in selected if int(r["run_id"]) == run_id]
        run.sort(key=lambda r: number(r.get("E_WE_RE_mV")))
        x = np.array([number(r.get("E_WE_RE_mV")) for r in run])
        y = np.array([number(r.get(column)) for r in run])
        valid = np.isfinite(x) & np.isfinite(y)
        traces.append((x[valid], y[valid]))
    return traces


def save_run_summary(rows: list[dict], stats: list[dict], output: Path) -> None:
    fields = [
        "profile", "label", "runs", "mean_delta_nA", "run_to_run_sd_nA",
        "run_to_run_cv_pct", "min_delta_nA", "max_delta_nA", "expected_delta_nA",
        "bias_nA", "bias_pct", "mean_error_pct", "mean_scan_s",
        "mean_within_scan_sd_nA",
    ]
    with (output / "dummy_blank_statistics.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(stats)

    fields = [
        "profile", "source_file", "run_id", "dummy_mean_delta_nA", "dummy_sd_delta_nA",
        "dummy_expected_delta_nA", "dummy_error_pct", "dummy_scan_ms", "dummy_boundary_jump_nA",
    ]
    with (output / "dummy_blank_run_summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def figure_repeatability(rows: list[dict], stats: list[dict], output: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)
    positions = np.arange(len(PROFILE_ORDER))
    for i, profile in enumerate(PROFILE_ORDER):
        selected = [number(r.get("dummy_mean_delta_nA")) for r in rows if r["profile"] == profile]
        if not selected:
            continue
        jitter = np.linspace(-0.11, 0.11, len(selected))
        axes[0].scatter(np.full(len(selected), i) + jitter, selected, color=PROFILE_COLOR[profile], s=42, zorder=3)
        stat = next(s for s in stats if s["profile"] == profile)
        axes[0].errorbar(i, stat["mean_delta_nA"], yerr=stat["run_to_run_sd_nA"], fmt="_", color="black", capsize=6, lw=2, zorder=4)
    axes[0].axhline(EXPECTED_NA, color="#b91c1c", ls="--", lw=1.5, label="Expected 70.187 nA")
    axes[0].set_xticks(positions, [PROFILE_LABEL[p] for p in PROFILE_ORDER])
    axes[0].set_ylabel("Mean dummy current, delta I (nA)")
    axes[0].set_title("15-run repeatability")
    axes[0].grid(axis="y", alpha=0.25)
    axes[0].legend(frameon=False)

    means = [s["mean_error_pct"] for s in stats]
    sds = [s["run_to_run_cv_pct"] for s in stats]
    axes[1].bar(positions, means, color=[PROFILE_COLOR[p] for p in PROFILE_ORDER], alpha=0.85)
    axes[1].set_xticks(positions, [PROFILE_LABEL[p] for p in PROFILE_ORDER])
    axes[1].set_ylabel("Mean absolute error (%)")
    axes[1].set_title("Accuracy and scan time")
    axes[1].set_ylim(0, max(means) * 1.18)
    axes[1].grid(axis="y", alpha=0.25)
    for i, (err, stat) in enumerate(zip(means, stats)):
        axes[1].text(i, err + max(means) * 0.025, f"{err:.2f}%\n{stat['mean_scan_s']:.2f} s", ha="center", va="bottom", fontsize=9)
    fig.suptitle("NanoStat dummy-cell blank: 995 kOhm, battery powered", fontsize=14)
    fig.savefig(output / "dummy_blank_repeatability.png", dpi=220)
    plt.close(fig)


def figure_traces(raw: list[dict], output: Path) -> None:
    fig, axes = plt.subplots(3, 1, figsize=(10, 11), sharex=False, constrained_layout=True)
    for ax, profile in zip(axes, PROFILE_ORDER):
        traces = raw_arrays(raw, profile)
        if not traces:
            ax.set_title(f"{PROFILE_LABEL[profile]}: no data")
            continue
        reference_x = traces[0][0]
        matrix = []
        for x, y in traces:
            if np.array_equal(x, reference_x):
                matrix.append(y)
            else:
                matrix.append(np.interp(reference_x, x, y))
        matrix = np.vstack(matrix)
        mean = np.nanmean(matrix, axis=0)
        sd = np.nanstd(matrix, axis=0, ddof=1)
        color = PROFILE_COLOR[profile]
        for y in matrix:
            ax.plot(reference_x, y, color="#9ca3af", alpha=0.45, lw=0.7)
        ax.fill_between(reference_x, mean - sd, mean + sd, color=color, alpha=0.16, label="mean +/- 1 SD")
        ax.plot(reference_x, mean, color=color, lw=2, label="15-run mean")
        ax.axhline(EXPECTED_NA, color="#b91c1c", ls="--", lw=1.2, label="expected")
        ax.set_title(PROFILE_LABEL[profile])
        ax.set_ylabel("Delta I (nA)")
        ax.grid(alpha=0.25)
        ax.legend(frameon=False, loc="best", fontsize=8)
    axes[-1].set_xlabel("E_WE-RE (mV)")
    fig.suptitle("Dummy-cell blank traces: 15 repeats per SWV profile", fontsize=14)
    fig.savefig(output / "dummy_blank_scan_traces.png", dpi=220)
    plt.close(fig)


def figure_timing_noise(raw: list[dict], stats: list[dict], output: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.8), constrained_layout=True)
    positions = np.arange(len(PROFILE_ORDER))
    durations = [s["mean_scan_s"] for s in stats]
    noise = []
    for profile in PROFILE_ORDER:
        values = []
        for _, y in raw_arrays(raw, profile):
            values.append(float(np.std(y, ddof=1)))
        noise.append(float(np.mean(values)))
    axes[0].bar(positions, durations, color=[PROFILE_COLOR[p] for p in PROFILE_ORDER])
    axes[0].set_xticks(positions, [PROFILE_LABEL[p] for p in PROFILE_ORDER])
    axes[0].set_ylabel("Mean scan duration (s)")
    axes[0].set_title("Scan duration")
    axes[0].grid(axis="y", alpha=0.25)
    axes[1].bar(positions, noise, color=[PROFILE_COLOR[p] for p in PROFILE_ORDER])
    axes[1].set_xticks(positions, [PROFILE_LABEL[p] for p in PROFILE_ORDER])
    axes[1].set_ylabel("Within-scan SD of Delta I (nA)")
    axes[1].set_title("Point-to-point variation")
    axes[1].grid(axis="y", alpha=0.25)
    fig.suptitle("Timing and raw-trace variation", fontsize=14)
    fig.savefig(output / "dummy_blank_timing_and_noise.png", dpi=220)
    plt.close(fig)


def write_report(stats: list[dict], files: list[dict], output: Path) -> None:
    lines = [
        "# Dummy blank analysis",
        "",
        f"Input files: {len(files)}; runs analyzed: {sum(s['runs'] for s in stats)}.",
        "Configuration: 995 kOhm dummy resistor, battery power, 120 Hz, external HSTIA path.",
        "Expected current is the firmware's 70.187 nA reference.",
        "",
        "| Profile | Runs | Mean delta I (nA) | Run-to-run SD (nA) | CV (%) | Mean error (%) | Scan (s) |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for s in stats:
        lines.append(
            f"| {s['label']} | {s['runs']} | {s['mean_delta_nA']:.3f} | {s['run_to_run_sd_nA']:.3f} | "
            f"{s['run_to_run_cv_pct']:.3f} | {s['mean_error_pct']:.3f} | {s['mean_scan_s']:.3f} |"
        )
    lines += [
        "",
        "Interpretation:",
        "- Run-to-run SD/CV quantify repeatability of the blank dummy measurement.",
        "- Mean error compares the measured Delta I with the firmware expected value.",
        "- The trace figure shows each run in gray, the 15-run mean in color, and the +/-1 SD band.",
        "- The 1 mV segmented profile takes longer because it contains many more potential steps.",
        "",
        "Input files:",
    ]
    lines.extend(f"- `{f['source_file']}` ({f['profile']})" for f in files)
    (output / "dummy_blank_analysis_report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    experiment_dir = Path(__file__).resolve().parents[1]
    parser.add_argument("--input-dir", type=Path, default=experiment_dir / "data")
    parser.add_argument("--output-dir", type=Path, default=experiment_dir / "legacy_output")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    summaries, raw, files = load_data(args.input_dir)
    stats = summary_stats(summaries)
    save_run_summary(summaries, stats, args.output_dir)
    figure_repeatability(summaries, stats, args.output_dir)
    figure_traces(raw, args.output_dir)
    figure_timing_noise(raw, stats, args.output_dir)
    write_report(stats, files, args.output_dir)
    print(f"Analyzed {len(files)} files and {sum(s['runs'] for s in stats)} runs")
    for stat in stats:
        print(
            f"{stat['label']}: mean={stat['mean_delta_nA']:.3f} nA, "
            f"run SD={stat['run_to_run_sd_nA']:.3f} nA, "
            f"error={stat['mean_error_pct']:.3f}%, scan={stat['mean_scan_s']:.3f} s"
        )


if __name__ == "__main__":
    main()

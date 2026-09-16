"""Analyse an RTT BATCUR trace and mark BLE/SWV workflow phases."""

from __future__ import annotations

import re
import csv
import statistics
from pathlib import Path
from collections import OrderedDict

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIGURES = ROOT / "figures"
INPUT = DATA / "00> *** Booting nRF Connect SDK v2.9.md"

BATCUR_RE = re.compile(r"BATCUR,t_ms=(\d+),IBAT_UA=(-?\d+|NA)")


def nearest_time(rows: list[dict], line_number: int) -> float | None:
    before = [row for row in rows if row["line"] <= line_number]
    after = [row for row in rows if row["line"] >= line_number]
    if before:
        return float(before[-1]["t_ms"])
    if after:
        return float(after[0]["t_ms"])
    return None


def marker_times(lines: list[str], rows: list[dict]) -> dict[str, float | None]:
    patterns = {
        "trace_start": "[BATCUR] Current trace STARTED",
        "ble_connected": "[BLE] Central connected.",
        "dummy_command": "RX command: START,DUMMY_REPEAT",
        # The first run is launched directly by the repeat-start handler; it
        # has no separate "Starting dummy run 1/5" line.
        "run1": "[DUMMY-SWV] Dummy resistor=995000 ohm",
        "run2": "[REPEAT] Starting dummy run 2/5.",
        "run3": "[REPEAT] Starting dummy run 3/5.",
        "run4": "[REPEAT] Starting dummy run 4/5.",
        "run5": "[REPEAT] Starting dummy run 5/5.",
        "run1_end": "[REPEAT] Run 1/5 complete.",
        "run2_end": "[REPEAT] Run 2/5 complete.",
        "run3_end": "[REPEAT] Run 3/5 complete.",
        "run4_end": "[REPEAT] Run 4/5 complete.",
        "run5_end": "[REPEAT] Completed 5/5 runs.",
        "complete": "[REPEAT] Completed 5/5 runs.",
        "ble_disconnected": "[BLE] Central disconnected",
        "trace_stop": "[BATCUR] Current trace STOPPED",
    }
    result: dict[str, float | None] = {}
    for name, pattern in patterns.items():
        line_number = next(
            (index + 1 for index, line in enumerate(lines) if pattern in line),
            None,
        )
        result[name] = nearest_time(rows, line_number) if line_number else None
    return result


def assign_phase(t_ms: float, markers: dict[str, float | None]) -> str:
    def t(name: str) -> float | None:
        return markers.get(name)

    if t("ble_disconnected") is not None and t_ms >= t("ble_disconnected"):
        return "BLE disconnected"
    if t("complete") is not None and t_ms >= t("complete"):
        return "Post-measurement connected"
    for run in range(5, 0, -1):
        start = t(f"run{run}")
        if start is not None and t_ms >= start:
            return f"SWV run {run}"
    if t("dummy_command") is not None and t_ms >= t("dummy_command"):
        return "SWV setup / quiet time"
    if t("ble_connected") is not None and t_ms >= t("ble_connected"):
        return "BLE connected idle"
    return "BLE advertising before connection"


def make_intervals(
    markers: dict[str, float | None], first_t: int, last_t: int
) -> list[dict[str, float | str]]:
    """Build non-overlapping workflow intervals for plotting and statistics."""

    def marker(name: str, fallback: int) -> int:
        value = markers.get(name)
        return int(value) if value is not None else fallback

    connected = marker("ble_connected", first_t)
    dummy_command = marker("dummy_command", connected)
    run_starts = [marker(f"run{run}", last_t) for run in range(1, 6)]
    complete = marker("complete", last_t)
    disconnected = marker("ble_disconnected", last_t)
    stopped = marker("trace_stop", last_t)

    intervals: list[dict[str, float | str]] = []

    def add(label: str, start: int, end: int, kind: str) -> None:
        start = max(first_t, min(start, last_t))
        end = max(start, min(end, last_t))
        if end > start:
            intervals.append({"label": label, "start_ms": start, "end_ms": end, "kind": kind})

    add("BLE advertising", first_t, connected, "advertising")
    add("BLE connected", connected, dummy_command, "connected")
    add("Setup / quiet", dummy_command, run_starts[0], "setup")

    for run in range(1, 6):
        start = run_starts[run - 1]
        if run < 5:
            end = marker(f"run{run}_end", run_starts[run])
            next_start = run_starts[run]
        else:
            end = marker("run5_end", complete)
            next_start = complete
        add(f"Run {run}", start, end, "run")
        if end < next_start:
            add(f"Gap {run}-{run + 1}", end, next_start, "gap")

    add("Post-runs", complete, disconnected, "post")
    add("BLE disconnected", disconnected, stopped, "disconnected")
    return intervals


def main() -> None:
    lines = INPUT.read_text(errors="replace").splitlines()
    records = []
    for line_number, line in enumerate(lines, start=1):
        match = BATCUR_RE.search(line)
        if not match:
            continue
        value = match.group(2)
        records.append(
            {
                "line": line_number,
                "t_ms": int(match.group(1)),
                "ibat_ua": None if value == "NA" else int(value),
            }
        )

    trace = [record for record in records if record["ibat_ua"] is not None]
    if not trace:
        raise SystemExit("No BATCUR records found")

    markers = marker_times(lines, trace)
    first_t = trace[0]["t_ms"]
    previous_t = None
    for row in trace:
        row["phase"] = assign_phase(row["t_ms"], markers)
        row["t_s"] = (row["t_ms"] - first_t) / 1000.0
        row["ibat_ma"] = row["ibat_ua"] / 1000.0
        row["dt_ms"] = None if previous_t is None else row["t_ms"] - previous_t
        previous_t = row["t_ms"]

    with (DATA / "power_trace_points.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["line", "t_ms", "ibat_ua", "phase", "t_s", "ibat_ma", "dt_ms"])
        writer.writeheader()
        writer.writerows(trace)

    phase_order = list(OrderedDict.fromkeys(row["phase"] for row in trace))
    summary_rows = []
    for phase in phase_order:
        values = [row["ibat_ma"] for row in trace if row["phase"] == phase]
        summary_rows.append(
            {
                "phase": phase,
                "count": len(values),
                "mean_mA": statistics.mean(values),
                "median_mA": statistics.median(values),
                "std_mA": statistics.pstdev(values),
                "min_mA": min(values),
                "max_mA": max(values),
            }
        )
    with (DATA / "power_trace_phase_summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary_rows[0]))
        writer.writeheader()
        writer.writerows(summary_rows)

    intervals = [row["dt_ms"] for row in trace if row["dt_ms"] is not None]
    sorted_intervals = sorted(intervals)
    p95_index = min(len(sorted_intervals) - 1, int(0.95 * (len(sorted_intervals) - 1)))
    values = [row["ibat_ma"] for row in trace]
    overview_rows = [
        ("trace_duration_s", (trace[-1]["t_ms"] - first_t) / 1000.0),
        ("point_count", len(trace)),
        ("median_sample_interval_ms", statistics.median(intervals)),
        ("p95_sample_interval_ms", sorted_intervals[p95_index]),
        ("intervals_over_200ms", sum(interval > 200 for interval in intervals)),
        ("overall_mean_mA", statistics.mean(values)),
        ("overall_median_mA", statistics.median(values)),
        ("overall_std_mA", statistics.pstdev(values)),
    ]
    with (DATA / "power_trace_overview.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "value"])
        writer.writerows(overview_rows)

    interval_definitions = make_intervals(markers, first_t, trace[-1]["t_ms"])
    interval_rows = []
    for interval in interval_definitions:
        start = int(interval["start_ms"])
        end = int(interval["end_ms"])
        selected = [row["ibat_ma"] for row in trace if start <= row["t_ms"] <= end]
        if not selected:
            continue
        interval_rows.append(
            {
                "label": interval["label"],
                "start_ms": start,
                "end_ms": end,
                "duration_s": (end - start) / 1000.0,
                "count": len(selected),
                "mean_mA": statistics.mean(selected),
                "median_mA": statistics.median(selected),
                "std_mA": statistics.pstdev(selected),
            }
        )
    with (DATA / "power_trace_interval_summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(interval_rows[0]))
        writer.writeheader()
        writer.writerows(interval_rows)

    FIGURES.mkdir(exist_ok=True)
    fig, ax = plt.subplots(figsize=(15, 7.2), dpi=180)
    ax.set_axisbelow(True)
    ax.plot([row["t_s"] for row in trace], [row["ibat_ma"] for row in trace],
            color="#7f8c99", linewidth=0.8, marker=".", markersize=2.0,
            alpha=0.42, label="Raw IBAT samples")
    ax.set_xlabel("Time from current trace start (s)")
    ax.set_ylabel("Battery current IBAT (mA)")
    ax.set_title("NanoStat battery current: workflow phases and interval means")
    ax.grid(True, alpha=0.25)

    colours = [
        "#90caf9", "#a5d6a7", "#ffe082", "#ce93d8", "#b0bec5",
        "#b39ddb", "#cfd8dc", "#ba68c8", "#cfd8dc", "#9575cd",
        "#cfd8dc", "#7e57c2", "#80cbc4", "#ef9a9a",
    ]
    for index, (interval, summary) in enumerate(zip(interval_definitions, interval_rows)):
        start_s = (int(interval["start_ms"]) - first_t) / 1000.0
        end_s = (int(interval["end_ms"]) - first_t) / 1000.0
        mean = float(summary["mean_mA"])
        std = float(summary["std_mA"])
        colour = colours[index % len(colours)]
        ax.axvspan(start_s, end_s, color=colour, alpha=0.13, linewidth=0)
        ax.axvline(start_s, color="#455a64", linestyle="--", linewidth=2.0, alpha=0.9)
        ax.plot([start_s, end_s], [mean, mean], color=colour, linewidth=4.0,
                solid_capstyle="butt", zorder=4)
        ax.fill_between([start_s, end_s], [mean - std, mean - std],
                        [mean + std, mean + std], color=colour, alpha=0.22,
                        linewidth=0, zorder=2)
        midpoint = (start_s + end_s) / 2.0
        ax.text(
            midpoint,
            0.98,
            str(interval["label"]),
            transform=ax.get_xaxis_transform(),
            ha="center",
            va="top",
            fontsize=7.5,
            rotation=90,
            color="#263238",
            bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.62, "pad": 1.2},
            zorder=5,
        )

    handles = [
        plt.Line2D([], [], color="#7f8c99", alpha=0.55, linewidth=1.2, label="Raw IBAT samples"),
        plt.Line2D([], [], color="#7e57c2", linewidth=4, label="Interval mean"),
        plt.Rectangle((0, 0), 1, 1, facecolor="#b39ddb", alpha=0.22, label="Mean ± 1 SD"),
    ]
    ax.legend(handles=handles, loc="lower right", fontsize=8, ncol=3)
    fig.subplots_adjust(top=0.78, right=0.98, left=0.08, bottom=0.12)
    fig.savefig(FIGURES / "power_current_trace.png", bbox_inches="tight")
    plt.close(fig)

    print("Markers:")
    for name, value in markers.items():
        print(f"  {name}: {value}")
    print("\nPhase summary:")
    for row in summary_rows:
        print(row)
    print("\nOverview:")
    for row in overview_rows:
        print(row[0], row[1])


if __name__ == "__main__":
    main()

"""Summarise the OLED-connected battery-current trace and compare it with the no-OLED trace."""

from __future__ import annotations

import csv
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
INPUT = DATA / "oled_battery_trace_rtt.md"
BASELINE = DATA / "power_trace_interval_summary.csv"

BATCUR_RE = re.compile(r"BATCUR,t_ms=(\d+),IBAT_UA=(-?\d+|NA)")


def nearest_time(lines: list[str], rows: list[dict], pattern: str) -> int | None:
    line_number = next(
        (index + 1 for index, line in enumerate(lines) if pattern in line), None
    )
    if line_number is None:
        return None
    before = [row for row in rows if row["line"] <= line_number]
    return before[-1]["t_ms"] if before else rows[0]["t_ms"]


def main() -> None:
    lines = INPUT.read_text(errors="replace").splitlines()
    rows = []
    for line_number, line in enumerate(lines, start=1):
        match = BATCUR_RE.search(line)
        if match and match.group(2) != "NA":
            rows.append(
                {
                    "line": line_number,
                    "t_ms": int(match.group(1)),
                    "ibat_mA": int(match.group(2)) / 1000.0,
                }
            )
    if not rows:
        raise SystemExit("No BATCUR records found")

    selection_line = next(
        (line for line in lines if "Dummy SWV repeat selected:" in line), ""
    )
    run_match = re.search(r"runs=(\d+)", selection_line)
    run_count = int(run_match.group(1)) if run_match else 1

    markers = {
        "trace_start": rows[0]["t_ms"],
        "ble_connected": nearest_time(lines, rows, "[BLE] Central connected."),
        "dummy_command": nearest_time(lines, rows, "RX command: START,DUMMY_REPEAT"),
        "run1": nearest_time(lines, rows, "[DUMMY-SWV] Dummy resistor=995000 ohm"),
        "ble_disconnected": nearest_time(lines, rows, "[BLE] Central disconnected"),
        "trace_stop": rows[-1]["t_ms"],
    }
    for run in range(2, run_count + 1):
        markers[f"run{run}"] = nearest_time(
            lines, rows, f"[REPEAT] Starting dummy run {run}/{run_count}."
        )
    for run in range(1, run_count):
        markers[f"run{run}_end"] = nearest_time(
            lines, rows, f"[REPEAT] Run {run}/{run_count} complete."
        )
    markers["complete"] = nearest_time(
        lines, rows, f"[REPEAT] Completed {run_count}/{run_count} runs."
    )

    intervals = [
        ("BLE advertising", markers["trace_start"], markers["ble_connected"]),
        ("BLE connected", markers["ble_connected"], markers["dummy_command"]),
        ("Setup / quiet", markers["dummy_command"], markers["run1"]),
    ]
    for run in range(1, run_count + 1):
        start = markers.get(f"run{run}")
        end = markers.get(f"run{run}_end") if run < run_count else markers["complete"]
        intervals.append((f"Run {run}", start, end))
        if run < run_count:
            intervals.append((f"Gap {run}-{run + 1}", end, markers.get(f"run{run + 1}")))
    intervals.extend(
        [
            ("Post-runs", markers["complete"], markers["ble_disconnected"]),
            ("BLE disconnected", markers["ble_disconnected"], markers["trace_stop"]),
        ]
    )

    summary = []
    for label, start, end in intervals:
        if start is None or end is None or end <= start:
            continue
        values = [row["ibat_mA"] for row in rows if start <= row["t_ms"] <= end]
        if not values:
            continue
        summary.append(
            {
                "condition": f"OLED connected; {run_count} runs",
                "label": label,
                "start_ms": start,
                "end_ms": end,
                "duration_s": (end - start) / 1000.0,
                "count": len(values),
                "mean_mA": statistics.mean(values),
                "median_mA": statistics.median(values),
                "std_mA": statistics.pstdev(values),
            }
        )

    output = DATA / "oled_power_trace_phase_summary.csv"
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)

    baseline = {}
    with BASELINE.open(newline="") as handle:
        for row in csv.DictReader(handle):
            baseline[row["label"]] = float(row["mean_mA"])

    comparison = []
    for row in summary:
        no_oled = baseline.get(row["label"])
        if no_oled is None:
            continue
        delta = row["mean_mA"] - no_oled
        comparison.append(
            {
                "label": row["label"],
                "no_oled_mean_mA": no_oled,
                "oled_mean_mA": row["mean_mA"],
                "delta_mA": delta,
                "delta_percent": (delta / no_oled * 100.0) if no_oled else "",
                "note": "Different run count/RTIA" if row["label"].startswith("Run") else "",
            }
        )

    comparison_output = DATA / "oled_vs_no_oled_comparison.csv"
    with comparison_output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(comparison[0]))
        writer.writeheader()
        writer.writerows(comparison)

    print(f"OLED phase summary ({run_count} runs):")
    for row in summary:
        print(row["label"], f"{row['mean_mA']:.3f} mA", f"n={row['count']}")
    print("\nBLE connected idle comparison:")
    connected = next(row for row in comparison if row["label"] == "BLE connected")
    print(
        f"no OLED={connected['no_oled_mean_mA']:.3f} mA, "
        f"OLED={connected['oled_mean_mA']:.3f} mA, "
        f"delta={connected['delta_mA']:.3f} mA "
        f"({connected['delta_percent']:.1f}%)"
    )


if __name__ == "__main__":
    main()
